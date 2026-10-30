#include "www/Migration.h"

const char* const TAG = "WwwMigration";

#include "hwutil/PartitionUtils.h"
#include "Logging.h"
#include "partition_layout/LiveTable.h"
#include "partition_layout/PartitionTable.h"
#include "www/WwwImage.h"
#include "www/WwwPartition.h"

#include <esp_partition.h>
#include <esp_system.h>

#include <array>
#include <cstring>
#include <memory>
#include <span>

using namespace OpenShock;
namespace PT = OpenShock::PartitionTable;

namespace {
  constexpr const char* kStaticLabel = "static0";  // the 1.x entry the www partition takes over

  // Downloads `url` into `partition` (FlashPartitionFromUrl: streamed, hashed, erased just ahead of the data), refusing
  // anything that does not start like a www image before the first erase; then checks what is on flash, so a write that
  // did not stick fails too.
  bool downloadInto(HTTP::Client& client, const esp_partition_t* partition, std::string_view url, const uint8_t (&hash)[32])
  {
    auto noProgress = [](std::size_t, std::size_t, float) -> bool {
      return true;
    };
    // A littlefs image (a 1.x release's staticfs) must not end up in what becomes www
    auto startsLikeWwwImage = [](std::span<const uint8_t> chunk) -> bool {
      uint32_t magic = 0;
      if (chunk.size() >= sizeof(magic)) {
        memcpy(&magic, chunk.data(), sizeof(magic));
      }
      if (magic != Www::kMagic) {
        OS_LOGE(TAG, "The download is not a www image");
        return false;
      }
      return true;
    };
    if (!FlashPartitionFromUrl(client, partition, url, hash, noProgress, startsLikeWwwImage)) {
      OS_LOGE(TAG, "Failed to download the www image");
      return false;
    }

    // What is on flash now: an image that parses, with the published hash
    std::array<uint8_t, 32> digest;
    if (!Www::ImageSha256(partition, digest) || memcmp(digest.data(), hash, 32) != 0) {
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
