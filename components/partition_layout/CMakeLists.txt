# (Named partition_layout: ESP-IDF owns the component name partition_table.)
# The ESP-IDF partition table as data: parse the binary table, tell which OpenShock layout it is, and check a
# staged table against the invariants the over-the-air table rewrite relies on (docs/STORAGE.md). Pure C++ over
# byte spans with its own MD5, so it has no ESP-IDF dependency and the host tests run it against every table in
# partitions/ plus a corpus of broken ones. LiveTable.cpp is the device side that reads what is on flash (not in
# the host tests); writing a new table is components/migration's.
idf_component_register(
  SRCS          "src/PartitionTable.cpp"
                "src/Md5.cpp"
                "src/LiveTable.cpp"
  INCLUDE_DIRS  "include"
  REQUIRES      common
  PRIV_REQUIRES logging spi_flash bootloader_support esp_partition   # OS_LOG*, esp_flash_read, the image header, the table offset
)
