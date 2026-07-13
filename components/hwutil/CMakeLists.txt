# Hardware-dependent utilities split out of `util` so that `util` stays
# pure std-C++ and host-testable. These pull in firmware (esp_partition, HTTP
# OTA) types that do not exist on the linux host target.
#
# Public surface:
#   include/hwutil/PartitionUtils.h — hash / flash an esp_partition from a URL

idf_component_register(
  SRCS          "src/PartitionUtils.cpp"
  INCLUDE_DIRS  "include"
  REQUIRES      esp_partition                # esp_partition_t in public header
  PRIV_REQUIRES common                       # Convert, StringHelpers, HexUtils
                crypto                       # Hashing
                temporal                         # Temporal.h
                http                         # HTTPRequestManager (URL flashing)
                logging                      # Logging.h
)
