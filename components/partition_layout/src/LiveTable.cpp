#include <freertos/FreeRTOS.h>

#include "partition_layout/LiveTable.h"

const char* const TAG = "PartitionTable";

#include "Logging.h"

#include <esp_flash.h>
#include <esp_flash_partitions.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

using namespace OpenShock;

namespace {
  constexpr int kWriteAttempts          = 3;
  constexpr TickType_t kWriteRetryDelay = pdMS_TO_TICKS(1'000);

  // Writes `table` through the OTA path, which lifts the dangerous-write protection for that one sector, erases it
  // and verifies the written table; then reads it back. False unless `table` is what is on flash.
  bool write(const esp_partition_t* pt, const PartitionTable::Bytes& table)
  {
    esp_err_t err           = ESP_OK;
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

    auto readBack = std::make_unique<PartitionTable::Bytes>();
    if (!PartitionTable::ReadLiveBytes(*readBack) || *readBack != table) {
      OS_LOGE(TAG, "The partition table does not read back as written");
      return false;
    }
    return true;
  }
}  // namespace

bool PartitionTable::ReadLiveBytes(Bytes& out)
{
  esp_err_t err = esp_flash_read(nullptr, out.data(), ESP_PRIMARY_PARTITION_TABLE_OFFSET, out.size());
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to read the partition table: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

std::unique_ptr<PartitionTable::Table> PartitionTable::ReadLive()
{
  auto bytes = std::make_unique<Bytes>();
  if (!ReadLiveBytes(*bytes)) {
    return nullptr;
  }
  auto table  = std::make_unique<Table>();
  auto parsed = Parse(*bytes, *table);
  if (!parsed) {
    OS_LOGE(TAG, "The live partition table does not parse: %s", ParseErrorName(parsed.error()));
    return nullptr;
  }
  return table;
}

PartitionTable::ReplaceResult PartitionTable::Replace(const Bytes& next, const Bytes& live)
{
  const esp_partition_t* pt = nullptr;
  esp_err_t err             = esp_partition_register_external(nullptr, ESP_PRIMARY_PARTITION_TABLE_OFFSET, ESP_PARTITION_TABLE_SIZE, "pt", ESP_PARTITION_TYPE_PARTITION_TABLE, ESP_PARTITION_SUBTYPE_PARTITION_TABLE_PRIMARY, &pt);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to register the partition table region: %s", esp_err_to_name(err));
    return ReplaceResult::Untouched;
  }

  for (int attempt = 0; attempt < kWriteAttempts; ++attempt) {
    if (write(pt, next)) {
      return ReplaceResult::Written;
    }
  }
  OS_LOGE(TAG, "The new partition table does not stick; writing the old one back");
  for (uint32_t attempt = 1;; ++attempt) {
    if (write(pt, live)) {
      return ReplaceResult::Restored;
    }
    // Whatever is on flash now may not parse: restarting would brick the hub. Keep trying.
    OS_LOGE(TAG, "Failed to write the old partition table back (attempt %u). Do not unplug the hub.", static_cast<unsigned>(attempt));
    vTaskDelay(kWriteRetryDelay);
  }
}
