#include <freertos/FreeRTOS.h>

#include "config/Migration.h"

const char* const TAG = "ConfigMigration";

#include "config/Section.h"
#include "config/Store.h"
#include "Logging.h"
#include "partition_layout/LiveTable.h"
#include "partition_layout/PartitionTable.h"
#include "SimpleMutex.h"

#include <esp_partition.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <cstring>
#include <memory>

using namespace OpenShock;
namespace PT = OpenShock::PartitionTable;

namespace {
  // The WiFi driver's own store from the Arduino builds: one STA profile, with the password in plain text. Nothing
  // reads it any more (CONFIG_ESP_WIFI_NVS_ENABLED=n).
  constexpr const char* kWifiDriverNamespace = "nvs.net80211";

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
}  // namespace

Config::MigrationResult Config::MigrateToCfgPartition()
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
  if (tables->parsed.Find("cfg") != nullptr) {
    return MigrationResult::NotNeeded;
  }
  const PT::Entry* config = tables->parsed.Find("config");
  if (config == nullptr || config->type != PT::kTypeData || config->subtype != PT::kSubtypeDataSpiffs) {
    OS_LOGE(TAG, "No 1.x littlefs config partition; not migrating");
    return MigrationResult::Refused;
  }
  // The retype hands the partition to NVS, and the next boot erases what littlefs left in it
  const char* store = StorageLabel();
  if (store == nullptr || strcmp(store, "nvs") != 0) {
    OS_LOGE(TAG, "The config is not in the shared nvs partition; retyping config would lose it");
    return MigrationResult::Refused;
  }

  tables->next = tables->live;
  if (auto retyped = PT::Retype(tables->next, "config", PT::kTypeData, PT::kSubtypeDataNvs, "cfg"); !retyped) {
    OS_LOGE(TAG, "Cannot retype the config partition: %s", PT::RetypeErrorName(retyped.error()));
    return MigrationResult::Refused;
  }

  // Quiesce: no config write can start and NVS is closed
  OS_LOGW(TAG, "Retyping the config partition as cfg. Do not unplug the hub.");
  ScopedLock configLock(&Internal::WriteLock());
  dropWifiDriverStore();
  nvs_flash_deinit();
  vTaskDelay(pdMS_TO_TICKS(100));

  switch (PT::Replace(tables->next, tables->live)) {
    case PT::ReplaceResult::Written:
      OS_LOGI(TAG, "Partition table rewritten; restarting into the cfg config store");
      break;
    case PT::ReplaceResult::Restored:
      OS_LOGE(TAG, "The partition table is the old one again; restarting on it");
      break;
    case PT::ReplaceResult::Untouched:
      OS_LOGE(TAG, "The partition table was not touched; restarting on it");
      break;
  }
  // NVS is closed: a restart is the one clean way back to a running hub
  esp_partition_unload_all();
  esp_restart();
}
