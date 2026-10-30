# The `www` image: the captive-portal assets as one flat, read-only directory, mapped from the `www`
# partition and served by pointer. Replaces the littlefs `static0` image: three files that are written
# whole by OTA and then only read need no filesystem, and a flat image reads straight from the flash
# cache instead of through littlefs' callback buffers.
#
#   include/www/WwwImage.h      the format: a parser/validator over a byte span, pure C++ (host-tested)
#   include/www/WwwPartition.h  the device side: find the partition, map it, verify, serve
#   include/www/Migration.h     writing the image over the 1.x static0 and retyping it as www (not wired up yet)
#
# scripts/utils/wwwimage.py writes the format; change both together.
idf_component_register(
  SRCS          "src/Migration.cpp"
                "src/WwwImage.cpp"
                "src/WwwPartition.cpp"
  INCLUDE_DIRS  "include"
  REQUIRES      common                     # OpenShock.h
                esp_partition              # esp_partition_t in WwwPartition.h
  PRIV_REQUIRES crypto logging             # SHA256 for the image hash; OS_LOG*
                spi_flash                  # esp_partition_mmap
                hwutil partition_layout    # the migration: FindStaticPartition/FlashPartitionFromUrl, retyping static0
                esp_system                 # esp_restart
)
