# Hardware/Arduino-dependent utilities split out of `util` so that `util` stays
# pure std-C++ and host-testable. These pull in Arduino (IPAddress) and firmware
# (esp_partition, HTTP OTA) types that do not exist on the linux host target.
#
# Public surface:
#   include/hwutil/IPAddressUtils.h — parse an IPv4 string into an Arduino IPAddress
#   include/hwutil/PartitionUtils.h — hash / flash an esp_partition from a URL

idf_component_register(
  SRCS          "src/IPAddressUtils.cpp"
                "src/PartitionUtils.cpp"
  INCLUDE_DIRS  "include"
  REQUIRES      framework-arduinoespressif32 # IPAddress.h in public header
                esp_partition                # esp_partition_t in public header
  PRIV_REQUIRES util                         # Convert, StringUtils, Hashing, HexUtils
                temporal                         # Temporal.h
                http                         # HTTPRequestManager (URL flashing)
                logging                      # Logging.h
)
