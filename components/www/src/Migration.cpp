#include "www/Migration.h"

const char* const TAG = "WwwMigration";

#include "hwutil/PartitionUtils.h"
#include "Logging.h"
#include "partition_layout/LiveTable.h"
#include "partition_layout/PartitionTable.h"
#include "www/WwwImage.h"
#include "www/WwwPartition.h"

#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>

#include <array>
#include <cstring>
#include <memory>
#include <span>

using namespace OpenShock;
namespace PT = OpenShock::PartitionTable;

namespace {
  constexpr const char* kStaticLabel    = "static0";  // the 1.x entry the www partition takes over
  constexpr std::size_t kCopyBufferSize = 4096;
  constexpr std::size_t kEraseStep      = 64 * 1024;

  bool copyImage(const esp_partition_t* source, const esp_partition_t* destination, std::size_t size)
  {
    std::array<uint8_t, kCopyBufferSize> buffer;
    std::size_t erasedTo = 0;
    for (std::size_t offset = 0; offset < size;) {
      const std::size_t length = std::min(buffer.size(), size - offset);
      while (erasedTo < offset + length) {
        const std::size_t eraseLength = std::min(kEraseStep, static_cast<std::size_t>(destination->size) - erasedTo);
        if (eraseLength == 0 || esp_partition_erase_range(destination, erasedTo, eraseLength) != ESP_OK) {
          OS_LOGE(TAG, "Failed to erase static0 at offset %zu", erasedTo);
          return false;
        }
        erasedTo += eraseLength;
      }
      if (esp_partition_read(source, offset, buffer.data(), length) != ESP_OK) {
        OS_LOGE(TAG, "Failed to read the staged www image");
        return false;
      }
      if (esp_partition_write(destination, offset, buffer.data(), length) != ESP_OK) {
        OS_LOGE(TAG, "Failed to write static0");
        return false;
      }
      offset += length;
    }
    return true;
  }

  // Downloads into the inactive app slot first. Its contents are disposable once this firmware has booted, while
  // static0 still holds the 1.x portal until the supplied image has passed both integrity checks.
  bool downloadInto(HTTP::Client& client, const esp_partition_t* staticPartition, std::string_view url, const uint8_t (&hash)[32])
  {
    const esp_partition_t* staging = esp_ota_get_next_update_partition(nullptr);
    if (staging == nullptr) {
      OS_LOGE(TAG, "No inactive OTA partition is available to stage the www image");
      return false;
    }

    auto noProgress = [](std::size_t, std::size_t, float) -> bool {
      return true;
    };
    auto startsLikeWwwImage = [](std::span<const uint8_t> chunk) -> bool {
      if (!Www::StartsLikeImage(chunk)) {
        OS_LOGE(TAG, "The download is not a www image");
        return false;
      }
      return true;
    };
    if (!FlashPartitionFromUrl(client, staging, url, hash, noProgress, startsLikeWwwImage)) {
      OS_LOGE(TAG, "Failed to download the www image into the staging partition");
      return false;
    }

    Www::Partition staged;
    if (!staged.mount(staging)) {
      OS_LOGE(TAG, "The staged www image does not parse");
      return false;
    }
    const std::size_t size = staged.image().ImageSize();
    if (size > staticPartition->size) {
      OS_LOGE(TAG, "The staged www image is too large for static0");
      return false;
    }

    OS_LOGI(TAG, "Writing the verified www image over static0 (0x%06x)", static_cast<unsigned>(staticPartition->address));
    if (!copyImage(staging, staticPartition, size)) {
      return false;
    }

    std::array<uint8_t, 32> digest;
    if (!Www::ImageSha256(staticPartition, digest) || memcmp(digest.data(), hash, 32) != 0) {
      OS_LOGE(TAG, "The www image does not read back as written");
      return false;
    }
    return true;
  }
}  // namespace

Www::MigrationResult Www::MigrateToWwwPartition(HTTP::Client& client, std::string_view url, const uint8_t (&sha256)[32])
{
  // Two raw tables (3 KiB each) and a parsed one (some 3.4 KiB) come to close to 9.5 KiB: on the heap, not the
  // caller's stack
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
  const esp_partition_t* staticPartition = FindStaticPartition();
  if (staticPartition == nullptr || staticPartition->address != entry->offset || staticPartition->size != entry->size) {
    OS_LOGE(TAG, "The running partition table does not match the one on flash; not migrating");
    return MigrationResult::Refused;
  }

  tables->next = tables->live;
  if (auto retyped = PT::Retype(tables->next, kStaticLabel, static_cast<uint8_t>(kPartitionType), static_cast<uint8_t>(kPartitionSubtype), kPartitionLabel); !retyped) {
    OS_LOGE(TAG, "Cannot retype the static0 partition: %s", PT::RetypeErrorName(retyped.error()));
    return MigrationResult::Refused;
  }

  // static0 is not touched until the image has downloaded and verified in the inactive OTA partition.
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
