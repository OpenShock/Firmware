# (Named partition_layout: ESP-IDF owns the component name partition_table.)
# The ESP-IDF partition table as data: parse the binary table and retype one entry in place, re-sealing its MD5,
# for the in-place table migrations (docs/STORAGE.md). PartitionTable.cpp is pure C++ over bytes with its own MD5,
# so the host tests run it unchanged. LiveTable.cpp is the device side: reading the table on flash and the guarded
# write that replaces it.
idf_component_register(
  SRCS          "src/PartitionTable.cpp"
                "src/Md5.cpp"
                "src/LiveTable.cpp"
  INCLUDE_DIRS  "include"
  PRIV_REQUIRES logging freertos spi_flash bootloader_support esp_partition app_update   # OS_LOG*, vTaskDelay, esp_flash_read, the table offset, the external partition, esp_ota_*
)
