#include <freertos/FreeRTOS.h>

#include "migration/LayoutMigration.h"

const char* const TAG = "LayoutMigration";

#include "captiveportal/Manager.h"
#include "config/Section.h"
#include "enums/FirmwareBootType.h"
#include "Hashing.h"
#include "http/HTTPRequestManager.h"
#include "Logging.h"
#include "OpenShock.h"
#include "OtaUpdateConfig.h"
#include "OtaUpdateManager.h"
#include "partition_layout/PartitionTable.h"
#include "SemVer.h"
#include "SimpleMutex.h"
#include "util/TaskUtils.h"
#include "wifi/WiFiManager.h"
#include "www/WwwImage.h"
#include "www/WwwPartition.h"

#include <esp_flash.h>
#include <esp_flash_partitions.h>
#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <sdkconfig.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <vector>

using namespace OpenShock;
namespace PT = OpenShock::PartitionTable;

namespace {
  constexpr uint32_t kCopyChunk    = 0x1000;
  constexpr uint32_t kEraseStep    = 0x10000;  // erase ahead of the data in steps this size, as PartitionUtils does
  constexpr uint32_t kMiB          = 1024 * 1024;
  constexpr TickType_t kWifiPoll   = pdMS_TO_TICKS(5'000);
  constexpr TickType_t kRetryDelay = pdMS_TO_TICKS(10 * 60'000);   // a network or server problem
  constexpr TickType_t kRefuseWait = pdMS_TO_TICKS(6 * 3600'000);  // the checks refused: only a new release changes that

  enum class Step : uint8_t {
    Done,        // nothing (more) to do here; the task ends
    Retry,       // try again after kRetryDelay
    RetryLater,  // the invariants refused; try again after kRefuseWait
  };

  // The WiFi driver's own store from the Arduino builds: one STA profile, with the password in plain text. Nothing
  // reads it any more (CONFIG_ESP_WIFI_NVS_ENABLED=n).
  constexpr const char* kWifiDriverNamespace = "nvs.net80211";

  std::optional<SemVer> targetVersion()
  {
    return TryParseSemVer(CONFIG_OPENSHOCK_MIGRATION_TARGET_VERSION);
  }

  bool isPivotBuild()
  {
    return Constants::FirmwareVersion().major < 2;
  }

  bool readLiveTable(std::array<uint8_t, PT::kMaxLen>& out)
  {
    esp_err_t err = esp_flash_read(nullptr, out.data(), ESP_PRIMARY_PARTITION_TABLE_OFFSET, out.size());
    if (err != ESP_OK) {
      OS_LOGE(TAG, "Failed to read the partition table: %s", esp_err_to_name(err));
      return false;
    }
    return true;
  }

  std::optional<PT::Table> liveTable()
  {
    std::array<uint8_t, PT::kMaxLen> bytes;
    if (!readLiveTable(bytes)) {
      return std::nullopt;
    }
    auto table = PT::Parse(bytes);
    if (!table) {
      OS_LOGE(TAG, "The live partition table does not parse: %s", PT::ParseErrorName(table.error()));
      return std::nullopt;
    }
    return *table;
  }

  // Length of the image in the running slot, from its own header (what has to fit wherever it goes).
  uint32_t runningImageLength(const esp_partition_t* running)
  {
    esp_image_metadata_t metadata {};
    const esp_partition_pos_t pos {.offset = running->address, .size = running->size};
    if (esp_image_get_metadata(&pos, &metadata) != ESP_OK) {
      return 0;
    }
    return metadata.image_len;
  }

  // Downloads `url` into raw flash at [address, address + size), checking its SHA-256 against `hash` and that it is a
  // www image; then hashes what is on flash again, so a write that did not stick fails too.
  bool downloadWwwToFlash(HTTP::Client& client, const std::string& url, const uint8_t (&hash)[32], uint32_t address, uint32_t size)
  {
    SHA256 sha;
    if (!sha.begin()) {
      return false;
    }
    std::size_t total = 0, erasedTo = 0;
    bool magicOk = false;

    auto onLength = [&](int length) -> bool {
      if (length <= 0 || static_cast<uint32_t>(length) > size) {
        OS_LOGE(TAG, "The www image (%d bytes) does not fit the www partition (%u bytes)", length, static_cast<unsigned>(size));
        return false;
      }
      total = static_cast<std::size_t>(length);
      return true;
    };
    auto onData = [&](std::size_t offset, const uint8_t* data, std::size_t length) -> bool {
      if (offset == 0) {
        // The first bytes are the magic: a littlefs image (a 1.x release's staticfs) must not land in www
        magicOk = length >= 4 && data[0] == 'O' && data[1] == 'S' && data[2] == 'W' && data[3] == 'W';
        if (!magicOk) {
          OS_LOGE(TAG, "The target release's staticfs artifact is not a www image");
          return false;
        }
      }
      while (erasedTo < offset + length) {
        const uint32_t step = std::min<uint32_t>(kEraseStep, size - erasedTo);
        if (step == 0 || esp_flash_erase_region(nullptr, address + erasedTo, step) != ESP_OK) {
          return false;
        }
        erasedTo += step;
      }
      return esp_flash_write(nullptr, data, address + offset, length) == ESP_OK && sha.update(data, length);
    };

    auto response = client.Download(
      url,
      {
        {"Accept", "application/octet-stream"}
    },
      onLength,
      onData,
      std::array<uint16_t, 1> {200},
      120'000
    );
    if (response.result != HTTP::RequestResult::Success || !magicOk) {
      OS_LOGE(TAG, "Failed to download the www image: %s [%d]", response.ResultToString(), response.code);
      return false;
    }
    std::array<uint8_t, 32> digest;
    if (!sha.finish(digest) || memcmp(digest.data(), hash, 32) != 0) {
      OS_LOGE(TAG, "The www image does not match its published hash");
      return false;
    }

    // What is on flash now
    SHA256 readBack;
    auto chunk = std::make_unique<uint8_t[]>(kCopyChunk);
    if (!readBack.begin()) {
      return false;
    }
    for (std::size_t offset = 0; offset < total; offset += kCopyChunk) {
      const std::size_t n = std::min<std::size_t>(kCopyChunk, total - offset);
      if (esp_flash_read(nullptr, chunk.get(), address + offset, n) != ESP_OK || !readBack.update(chunk.get(), n)) {
        return false;
      }
    }
    return readBack.finish(digest) && memcmp(digest.data(), hash, 32) == 0;
  }

  // Downloads the partition table into RAM and checks its SHA-256 against the release's manifest.
  bool downloadTable(HTTP::Client& client, const std::string& url, const uint8_t (&hash)[32], std::array<uint8_t, PT::kMaxLen>& out)
  {
    std::size_t length = 0;
    out.fill(0xFF);
    auto onLength = [&](int contentLength) {
      return contentLength > 0 && static_cast<std::size_t>(contentLength) <= PT::kMaxLen;
    };
    auto onData = [&](std::size_t offset, const uint8_t* data, std::size_t n) {
      if (offset + n > out.size()) {
        return false;
      }
      memcpy(out.data() + offset, data, n);
      length = std::max(length, offset + n);
      return true;
    };
    auto response = client.Download(
      url,
      {
        {"Accept", "application/octet-stream"}
    },
      onLength,
      onData,
      std::array<uint16_t, 1> {200},
      30'000
    );
    if (response.result != HTTP::RequestResult::Success) {
      OS_LOGE(TAG, "Failed to download the partition table: %s [%d]", response.ResultToString(), response.code);
      return false;
    }
    SHA256 sha;
    std::array<uint8_t, 32> digest;
    if (!sha.begin() || !sha.update(out.data(), length) || !sha.finish(digest) || memcmp(digest.data(), hash, 32) != 0) {
      OS_LOGE(TAG, "The partition table does not match its published hash");
      return false;
    }
    return true;
  }

  bool artifactAnswers(HTTP::Client& client, const std::string& url, const char* what)
  {
    auto response = client.Head(url, {}, std::array<uint16_t, 1> {200});
    if (response.result != HTTP::RequestResult::Success) {
      OS_LOGW(TAG, "The target release's %s does not answer: %s [%d]", what, response.ResultToString(), response.code);
      return false;
    }
    return true;
  }

  // Requires everything else quiesced. Writes `table` over the live one through the OTA path, which lifts the
  // dangerous-write protection for that one sector, erases it and verifies the written table; then reads it back.
  bool writeTable(const std::array<uint8_t, PT::kMaxLen>& table)
  {
    const esp_partition_t* pt = nullptr;
    esp_err_t err             = esp_partition_register_external(nullptr, ESP_PRIMARY_PARTITION_TABLE_OFFSET, ESP_PARTITION_TABLE_SIZE, "pt", ESP_PARTITION_TYPE_PARTITION_TABLE, ESP_PARTITION_SUBTYPE_PARTITION_TABLE_PRIMARY, &pt);
    if (err != ESP_OK) {
      OS_LOGE(TAG, "Failed to register the partition table region: %s", esp_err_to_name(err));
      return false;
    }

    esp_ota_handle_t handle = 0;
    // ---- From the erase in esp_ota_begin until esp_ota_write completes, a power cut leaves no partition table ----
    err = esp_ota_begin(pt, ESP_PARTITION_TABLE_MAX_LEN, &handle);
    if (err == ESP_OK) {
      err = esp_ota_write(handle, table.data(), ESP_PARTITION_TABLE_MAX_LEN);
      // ---- The table is whole again from here ----
      esp_err_t endErr = esp_ota_end(handle);
      if (err == ESP_OK) {
        err = endErr;
      }
    }
    if (err != ESP_OK) {
      OS_LOGE(TAG, "Failed to write the partition table: %s", esp_err_to_name(err));
      return false;
    }

    std::array<uint8_t, PT::kMaxLen> readBack;
    if (!readLiveTable(readBack) || readBack != table) {
      OS_LOGE(TAG, "The partition table does not read back as written");
      return false;
    }
    return true;
  }

  // ---------------------------------------------------------------------------------------------------------------
  // On the 2.x table, running the pivot
  // ---------------------------------------------------------------------------------------------------------------

  void dropWifiDriverStore()
  {
    nvs_handle_t handle = 0;
    if (nvs_open_from_partition("nvs", kWifiDriverNamespace, NVS_READWRITE, &handle) != ESP_OK) {
      return;  // Not there: never written, or already dropped
    }
    if (nvs_erase_all(handle) == ESP_OK && nvs_commit(handle) == ESP_OK) {
      OS_LOGI(TAG, "Dropped the WiFi driver's stale NVS store (%s)", kWifiDriverNamespace);
    }
    nvs_close(handle);
  }

  Step finishOnNewLayout(const SemVer& target)
  {
    if (OtaUpdateManager::GetFirmwareBootType() == FirmwareBootType::Rollback) {
      OS_LOGE(TAG, "The target firmware rolled back to this one; not installing it again. Update the hub by hand.");
      return Step::Done;
    }

    dropWifiDriverStore();

    auto ota = Config::GetOtaUpdateConfig();
    if (!ota->isEnabled) {
      OS_LOGW(TAG, "OTA updates are disabled; the hub stays on this firmware until they are enabled again");
      return Step::RetryLater;
    }
    if (ota->updateStep == OtaUpdateStep::Updating) {
      return Step::Retry;  // Already installing
    }
    // The OTA task does the install: www image (unchanged, so skipped), then the app, then the restart.
    if (!OtaUpdateManager::TryStartMigrationUpdate(target)) {
      return Step::Retry;
    }
    return Step::Retry;  // If the install fails, ask again later; a successful one restarts the hub
  }

  // ---------------------------------------------------------------------------------------------------------------
  // On the 1.x table
  // ---------------------------------------------------------------------------------------------------------------

  Step migrateFromV1(const SemVer& target, const PT::Table& live)
  {
    auto ota = Config::GetOtaUpdateConfig();
    if (!ota->isEnabled) {
      OS_LOGW(TAG, "OTA updates are disabled; not migrating");
      return Step::RetryLater;
    }
    if (ota->updateStep == OtaUpdateStep::Updating || ota->updateStep == OtaUpdateStep::Updated) {
      return Step::Retry;  // An update is in flight
    }

    HTTP::Client client;
    auto release = OtaUpdateManager::TryGetFirmwareRelease(client, target);
    if (!release) {
      OS_LOGW(TAG, "The target release %s is not available for this board yet", CONFIG_OPENSHOCK_MIGRATION_TARGET_VERSION);
      return Step::Retry;
    }
    if (release->partitionTableUrl.empty()) {
      OS_LOGE(TAG, "The target release carries no partition table for this board; not migrating");
      return Step::RetryLater;
    }
    // Never move to the new layout with nothing to install there
    if (!artifactAnswers(client, release->appBinaryUrl, "app") || !artifactAnswers(client, release->filesystemBinaryUrl, "www image")) {
      return Step::Retry;
    }

    std::array<uint8_t, PT::kMaxLen> stagedBytes;
    if (!downloadTable(client, release->partitionTableUrl, release->partitionTableHash, stagedBytes)) {
      return Step::Retry;
    }
    auto staged = PT::Parse(stagedBytes);
    if (!staged) {
      OS_LOGE(TAG, "The target partition table does not parse: %s", PT::ParseErrorName(staged.error()));
      return Step::RetryLater;
    }

    const esp_partition_t* running = esp_ota_get_running_partition();
    uint32_t flashSize             = 0;
    esp_flash_get_size(nullptr, &flashSize);
    const PT::Context context {
      .flashSize           = flashSize,
      .bootloaderFlashSize = Migration::BootloaderFlashSize(),
      .runningOffset       = running->address,
      .runningImageSize    = runningImageLength(running),
      .live                = &live,
    };
    OS_LOGI(
      TAG,
      "Flash %u MiB, bootloader header %u MiB, running %s at 0x%06x (%u bytes)",
      static_cast<unsigned>(flashSize / kMiB),
      static_cast<unsigned>(context.bootloaderFlashSize / kMiB),
      running->label,
      static_cast<unsigned>(running->address),
      static_cast<unsigned>(context.runningImageSize)
    );
    if (context.runningImageSize == 0) {
      OS_LOGE(TAG, "Cannot read the running image's length");
      return Step::RetryLater;
    }

    // Nothing moves (the checks hold the new table to the old geometry), so the running image stays where it is
    auto verdict = PT::Validate(*staged, context);
    if (!verdict) {
      OS_LOGE(TAG, "The target partition table fails a check: %s; staying on the 1.x layout", PT::ViolationName(verdict.error()));
      return Step::RetryLater;
    }

    // From here the portal's 1.x assets are overwritten (www is the 1.x static0): the portal stays closed until the restart
    if (!CaptivePortal::ForceClose(5000)) {
      OS_LOGE(TAG, "Failed to close the captive portal");
      return Step::Retry;
    }
    const PT::Entry* www = staged->Find(PT::kTypeWww, 0xFF, "www");
    OS_LOGI(TAG, "Writing the portal assets over the 1.x static0, where the new table puts www (0x%06x)", static_cast<unsigned>(www->offset));
    if (!downloadWwwToFlash(client, release->filesystemBinaryUrl, release->filesystemBinaryHash, www->offset, www->size)) {
      CaptivePortal::ReleaseForceClose();
      return Step::Retry;
    }

    // Quiesce: no config write can start, NVS is closed, nothing else touches the partition API
    OS_LOGW(TAG, "Rewriting the partition table. Do not unplug the hub.");
    ScopedLock configLock(&Config::Internal::WriteLock());
    nvs_flash_deinit();
    vTaskDelay(pdMS_TO_TICKS(100));

    if (!writeTable(stagedBytes)) {
      // Still the old table (esp_ota_begin failed before erasing) or a corrupt one; either way restart: the next boot
      // tells which, and an unbootable hub needs the serial recovery (docs/STORAGE.md).
      OS_LOGE(TAG, "The partition table write failed; restarting");
    } else {
      OS_LOGI(TAG, "Partition table rewritten; restarting into the new layout");
    }
    esp_partition_unload_all();
    esp_restart();
    return Step::Done;  // Unreachable
  }

  void waitForWifi()
  {
    while (!WiFiManager::IsConnected()) {
      vTaskDelay(kWifiPoll);
    }
  }

  void migrationTask(void*)
  {
    const auto target = targetVersion();
    while (target) {
      waitForWifi();
      auto live = liveTable();
      Step step = Step::Done;
      if (!live) {
        step = Step::RetryLater;
      } else {
        switch (PT::Detect(*live)) {
          case PT::Layout::V1:
            step = migrateFromV1(*target, *live);
            break;
          case PT::Layout::V2:
            step = finishOnNewLayout(*target);
            break;
          case PT::Layout::Unknown:
            OS_LOGE(TAG, "This hub's partition table is no layout this firmware knows; not migrating");
            step = Step::Done;
            break;
        }
      }
      if (step == Step::Done) {
        break;
      }
      vTaskDelay(step == Step::Retry ? kRetryDelay : kRefuseWait);
    }
    vTaskDelete(nullptr);
  }
}  // namespace

uint32_t Migration::BootloaderFlashSize()
{
  uint8_t header[4] = {};
  if (esp_flash_read(nullptr, header, CONFIG_BOOTLOADER_OFFSET_IN_FLASH, sizeof(header)) != ESP_OK || header[0] != ESP_IMAGE_HEADER_MAGIC) {
    return 0;
  }
  const uint8_t code = header[3] >> 4;  // esp_image_flash_size_t: 0 = 1 MB, 1 = 2 MB, 2 = 4 MB, ...
  return code <= 7 ? (kMiB << code) : 0;
}

const char* Migration::LiveLayoutName()
{
  auto live = liveTable();
  return live ? PT::LayoutName(PT::Detect(*live)) : "unreadable";
}

bool Migration::Start()
{
  auto live               = liveTable();
  const PT::Layout layout = live ? PT::Detect(*live) : PT::Layout::Unknown;
  OS_LOGI(TAG, "Partition layout: %s; bootloader header declares %u MiB", PT::LayoutName(layout), static_cast<unsigned>(BootloaderFlashSize() / kMiB));

  if (!isPivotBuild()) {
    if (layout != PT::Layout::V2) {
      OS_LOGE(TAG, "This firmware expects a 2.x partition layout; flash the merged image for this board over USB");
    }
    return true;
  }
  if (!targetVersion()) {
    OS_LOGE(TAG, "CONFIG_OPENSHOCK_MIGRATION_TARGET_VERSION (%s) is not a version", CONFIG_OPENSHOCK_MIGRATION_TARGET_VERSION);
    return false;
  }

  if (TaskUtils::TaskCreateExpensive(migrationTask, "LayoutMigration", 12'288, nullptr, 1, nullptr) != pdPASS) {
    OS_LOGE(TAG, "Failed to create the migration task");
    return false;
  }
  return true;
}
