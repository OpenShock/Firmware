#include <freertos/FreeRTOS.h>

#include "migration/StorageMigration.h"

const char* const TAG = "StorageMigration";

#include "captiveportal/Manager.h"
#include "config/Migration.h"
#include "http/HTTPRequestManager.h"
#include "Logging.h"
#include "OpenShock.h"
#include "OtaUpdateManager.h"
#include "partition_layout/LiveTable.h"
#include "partition_layout/PartitionTable.h"
#include "SimpleMutex.h"
#include "util/TaskUtils.h"
#include "wifi/WiFiManager.h"
#include "www/Migration.h"

#include <freertos/task.h>

#include <cstring>
#include <memory>
#include <optional>

using namespace OpenShock;
namespace PT = OpenShock::PartitionTable;

namespace {
  constexpr TickType_t kWifiPoll   = pdMS_TO_TICKS(5'000);
  constexpr TickType_t kRetryDelay = pdMS_TO_TICKS(10 * 60'000);  // a network, server or busy-hub problem

  struct WwwSource {
    std::string url;
    uint8_t sha256[32];
  };

  TaskHandle_t s_task = nullptr;
  SimpleMutex s_requestMutex {};
  std::optional<WwwSource> s_requestedWww;  // Guarded by s_requestMutex
  bool s_saidNoRelease = false;

  struct Pending {
    bool cfg;  // `config` is still the 1.x littlefs and there is no `cfg`
    bool www;  // `static0` is still the 1.x littlefs and there is no `www`
  };

  // What the live table still has to migrate; nullopt when it cannot be read.
  std::optional<Pending> readPending()
  {
    struct Tables {
      PT::Bytes live;
      PT::Table parsed;
    };
    auto tables = std::make_unique<Tables>();
    if (!PT::ReadLiveBytes(tables->live) || !PT::Parse(tables->live, tables->parsed)) {
      OS_LOGE(TAG, "Cannot read the partition table");
      return std::nullopt;
    }
    auto isLittlefs = [](const PT::Entry* entry) {
      return entry != nullptr && entry->type == PT::kTypeData && entry->subtype == PT::kSubtypeDataSpiffs;
    };
    return Pending {
      .cfg = tables->parsed.Find("cfg") == nullptr && isLittlefs(tables->parsed.Find("config")),
      .www = tables->parsed.Find("www") == nullptr && isLittlefs(tables->parsed.Find("static0")),
    };
  }

  std::optional<WwwSource> takeRequestedWww()
  {
    ScopedLock lock(&s_requestMutex);
    auto taken = std::move(s_requestedWww);
    s_requestedWww.reset();
    return taken;
  }

  // The running release's www image, for a build that has one published.
  std::optional<WwwSource> releaseWww(HTTP::Client& client)
  {
    const SemVer& version = Constants::FirmwareVersion();
    if (version.major < 2) {
      if (!s_saidNoRelease) {
        OS_LOGW(TAG, "No www image is published for firmware %s; waiting for `migrate www <url> <sha256>` over serial", Constants::FW_VERSION);
        s_saidNoRelease = true;
      }
      return std::nullopt;
    }
    auto release = OtaUpdateManager::TryGetFirmwareRelease(client, version);
    if (!release) {
      OS_LOGW(TAG, "The release for firmware %s is not available; retrying later", Constants::FW_VERSION);
      return std::nullopt;
    }
    WwwSource source;
    source.url = std::move(release->filesystemBinaryUrl);
    memcpy(source.sha256, release->filesystemBinaryHash, sizeof(source.sha256));
    return source;
  }

  // Both steps hold the OTA flash-write lock from before their first flash write to the restart, and give it back on
  // every return (every return is a failure: success ends in esp_restart).
  struct FlashWriteLock {
    bool held = OtaUpdateManager::TryLockFlashWrites();
    ~FlashWriteLock()
    {
      if (held) OtaUpdateManager::UnlockFlashWrites();
    }
  };

  // Returns only when nothing was migrated (the step restarts the hub when it was)
  void migrateCfg()
  {
    FlashWriteLock lock;
    if (!lock.held) {
      OS_LOGI(TAG, "An update is being installed; migrating the config partition later");
      return;
    }
    switch (Config::MigrateToCfgPartition()) {
      case Config::MigrationResult::NotNeeded:
        break;
      case Config::MigrationResult::Refused:
        OS_LOGE(TAG, "The config partition migration was refused; the config stays in the shared nvs");
        break;
    }
  }

  void migrateWww()
  {
    HTTP::Client client;
    auto source = takeRequestedWww();
    if (!source) {
      source = releaseWww(client);
    }
    if (!source) {
      return;
    }

    FlashWriteLock lock;
    if (!lock.held) {
      OS_LOGI(TAG, "An update is being installed; migrating the www partition later");
      return;
    }
    // From here the 1.x assets in static0 are overwritten: the portal stays closed until the restart
    if (!CaptivePortal::ForceClose(5000)) {
      OS_LOGE(TAG, "Failed to close the captive portal; migrating the www partition later");
      return;
    }
    OS_LOGI(TAG, "Migrating the www partition from %s", source->url.c_str());
    switch (Www::MigrateToWwwPartition(client, source->url, source->sha256)) {
      case Www::MigrationResult::NotNeeded:
        break;
      case Www::MigrationResult::Refused:
        OS_LOGE(TAG, "The www partition migration was refused; the portal keeps serving from static0");
        break;
      case Www::MigrationResult::Failed:
        OS_LOGE(TAG, "The www image did not download or verify; static0 holds no portal assets until a retry succeeds");
        break;
    }
    CaptivePortal::ReleaseForceClose();
  }

  void migrationTask(void*)
  {
    while (true) {
      auto pending = readPending();
      if (!pending) {
        // Unreadable: nothing can be decided; look again later
      } else if (pending->cfg) {
        migrateCfg();
      } else if (pending->www) {
        if (!WiFiManager::IsConnected()) {
          TaskUtils::WaitForNotify(kWifiPoll);  // Consumes a request made meanwhile, so it does not cut the retry delay short later
          continue;
        }
        migrateWww();
      } else {
        OS_LOGI(TAG, "The storage migration is complete");
        break;
      }
      // Each step restarts the hub on success; being here means it did not happen this round
      TaskUtils::WaitForNotify(kRetryDelay);
    }
    s_task = nullptr;
    vTaskDelete(nullptr);
  }
}  // namespace

bool Migration::Start()
{
  auto pending = readPending();
  if (!pending) {
    return false;
  }
  if (!pending->cfg && !pending->www) {
    OS_LOGD(TAG, "The partition table is the 2.x layout; nothing to migrate");
    return true;
  }
  OS_LOGI(TAG, "This hub still has the 1.x %s%s%s partition; the storage migration is pending", pending->cfg ? "config" : "", pending->cfg && pending->www ? " and " : "", pending->www ? "static0" : "");
  if (TaskUtils::TaskCreateExpensive(migrationTask, "StorageMigration", 8'192, nullptr, 1, &s_task) != pdPASS) {
    OS_LOGE(TAG, "Failed to create the migration task");
    return false;
  }
  return true;
}

bool Migration::IsPending()
{
  return s_task != nullptr;
}

bool Migration::RequestWww(std::string url, const uint8_t (&sha256)[32])
{
  if (s_task == nullptr) {
    return false;
  }
  {
    ScopedLock lock(&s_requestMutex);
    s_requestedWww.emplace();
    s_requestedWww->url = std::move(url);
    memcpy(s_requestedWww->sha256, sha256, sizeof(s_requestedWww->sha256));
  }
  return RequestNow();
}

bool Migration::RequestNow()
{
  TaskHandle_t task = s_task;
  if (task == nullptr) {
    return false;
  }
  TaskUtils::Notify(task);
  return true;
}
