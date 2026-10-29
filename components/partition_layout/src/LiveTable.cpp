#include "partition_layout/LiveTable.h"

const char* const TAG = "PartitionTable";

#include "Logging.h"

#include <esp_flash.h>
#include <esp_flash_partitions.h>
#include <esp_image_format.h>
#include <sdkconfig.h>

using namespace OpenShock;

bool PartitionTable::ReadLiveBytes(std::array<uint8_t, kMaxLen>& out)
{
  esp_err_t err = esp_flash_read(nullptr, out.data(), ESP_PRIMARY_PARTITION_TABLE_OFFSET, out.size());
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to read the partition table: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

std::optional<PartitionTable::Table> PartitionTable::ReadLive()
{
  std::array<uint8_t, kMaxLen> bytes;
  if (!ReadLiveBytes(bytes)) {
    return std::nullopt;
  }
  auto table = Parse(bytes);
  if (!table) {
    OS_LOGE(TAG, "The live partition table does not parse: %s", ParseErrorName(table.error()));
    return std::nullopt;
  }
  return *table;
}

PartitionTable::Layout PartitionTable::LiveLayout()
{
  auto live = ReadLive();
  return live ? Detect(*live) : Layout::Unknown;
}

uint32_t PartitionTable::BootloaderFlashSize()
{
  uint8_t header[4] = {};
  if (esp_flash_read(nullptr, header, CONFIG_BOOTLOADER_OFFSET_IN_FLASH, sizeof(header)) != ESP_OK || header[0] != ESP_IMAGE_HEADER_MAGIC) {
    return 0;
  }
  const uint8_t code = header[3] >> 4;  // esp_image_flash_size_t: 0 = 1 MB, 1 = 2 MB, 2 = 4 MB, ...
  return code <= 7 ? ((1024u * 1024u) << code) : 0;
}
