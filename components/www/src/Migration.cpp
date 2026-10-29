#include <freertos/FreeRTOS.h>

#include "www/Migration.h"

const char* const TAG = "WwwMigration";

#include "Hashing.h"
#include "http/HTTPRequestManager.h"
#include "Logging.h"
#include "partition_layout/LiveTable.h"
#include "partition_layout/PartitionTable.h"
#include "www/WwwImage.h"
#include "www/WwwPartition.h"

#include <esp_partition.h>
#include <esp_system.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

using namespace OpenShock;
namespace PT = OpenShock::PartitionTable;

namespace {
  constexpr uint32_t kEraseStep      = 0x10000;  // erase ahead of the data in steps this size, as PartitionUtils does
  constexpr const char* kStaticLabel = "static0";

  // Downloads `url` into `partition`, checking that it is a www image and that its SHA-256 matches `hash`; then checks
  // what is on flash, so a write that did not stick fails too.
  bool downloadInto(HTTP::Client& client, const esp_partition_t* partition, std::string_view url, const uint8_t (&hash)[32])
  {
    SHA256 sha;
    if (!sha.begin()) {
      return false;
    }
    std::size_t erasedTo = 0;
    bool magicOk         = false;

    auto onLength = [&](int length) -> bool {
      if (length <= 0 || static_cast<uint32_t>(length) > partition->size) {
        OS_LOGE(TAG, "The www image (%d bytes) does not fit %s (%u bytes)", length, partition->label, static_cast<unsigned>(partition->size));
        return false;
      }
      return true;
    };
    auto onData = [&](std::size_t offset, const uint8_t* data, std::size_t length) -> bool {
      if (offset == 0) {
        // A littlefs image (a 1.x release's staticfs) must not end up in what becomes www
        uint32_t magic = 0;
        magicOk        = length >= sizeof(magic) && (memcpy(&magic, data, sizeof(magic)), magic == Www::kMagic);
        if (!magicOk) {
          OS_LOGE(TAG, "The download is not a www image");
          return false;
        }
      }
      while (erasedTo < offset + length) {
        const std::size_t step = std::min<std::size_t>(kEraseStep, partition->size - erasedTo);
        if (step == 0 || esp_partition_erase_range(partition, erasedTo, step) != ESP_OK) {
          return false;
        }
        erasedTo += step;
      }
      return esp_partition_write(partition, offset, data, length) == ESP_OK && sha.update(data, length);
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

    // What is on flash now: an image that parses, with the published hash
    if (!Www::ImageSha256(partition, digest) || memcmp(digest.data(), hash, 32) != 0) {
      OS_LOGE(TAG, "The www image does not read back as written");
      return false;
    }
    return true;
  }
}  // namespace

Www::MigrationResult Www::MigrateToWwwPartition(HTTP::Client& client, std::string_view url, const uint8_t (&sha256)[32])
{
  // A raw table and a parsed one come to some 6.5 KiB: on the heap, not the caller's stack
  struct Tables {
    PT::Bytes live;
    PT::Bytes next;
    PT::Table parsed;
  };
  auto tables = std::make_unique<Tables>();

  if (!PT::ReadLiveBytes(tables->live) || !PT::Parse(tables->live, tables->parsed)) {
    OS_LOGE(TAG, "Cannot read the partition table; not migrating");
    return MigrationResult::Refused;
  }
  if (tables->parsed.Find(kPartitionLabel) != nullptr) {
    return MigrationResult::NotNeeded;
  }
  const PT::Entry* entry = tables->parsed.Find(kStaticLabel);
  if (entry == nullptr || entry->type != PT::kTypeData || entry->subtype != PT::kSubtypeDataSpiffs) {
    OS_LOGE(TAG, "No 1.x littlefs static0 partition; not migrating");
    return MigrationResult::Refused;
  }
  const esp_partition_t* staticPartition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, kStaticLabel);
  if (staticPartition == nullptr || staticPartition->address != entry->offset || staticPartition->size != entry->size) {
    OS_LOGE(TAG, "The running partition table does not match the one on flash; not migrating");
    return MigrationResult::Refused;
  }

  tables->next = tables->live;
  if (auto retyped = PT::Retype(tables->next, kStaticLabel, static_cast<uint8_t>(kPartitionType), static_cast<uint8_t>(kPartitionSubtype), kPartitionLabel); !retyped) {
    OS_LOGE(TAG, "Cannot retype the static0 partition: %s", PT::RetypeErrorName(retyped.error()));
    return MigrationResult::Refused;
  }

  // From here the 1.x assets are gone
  OS_LOGI(TAG, "Writing the www image over static0 (0x%06x)", static_cast<unsigned>(staticPartition->address));
  if (!downloadInto(client, staticPartition, url, sha256)) {
    return MigrationResult::Failed;
  }

  OS_LOGW(TAG, "Retyping the static0 partition as www. Do not unplug the hub.");
  switch (PT::Replace(tables->next, tables->live)) {
    case PT::ReplaceResult::Written:
      OS_LOGI(TAG, "Partition table rewritten; restarting into the www partition");
      break;
    case PT::ReplaceResult::Restored:
      OS_LOGE(TAG, "The partition table is the old one again; restarting on it");
      break;
    case PT::ReplaceResult::Untouched:
      OS_LOGE(TAG, "The partition table was not touched; restarting on it");
      break;
  }
  // The running firmware keeps the table it loaded at boot: a restart is the one clean way onto the new one
  esp_partition_unload_all();
  esp_restart();
}
