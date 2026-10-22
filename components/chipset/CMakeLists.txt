# OpenShock chipset utilities: GPIO validation against the chip's reserved pins and the
# board's bypass list.
#
# Public surface:
#   Chipset.h: OpenShock::IsValidInputPin / IsValidOutputPin per IDF_TARGET, plus the
#              board's bypasses from the descriptor (BoardDescriptor.h in common).
#
# BoardChecks.cpp validates the running board descriptor at boot (ValidateBoardDescriptor).
# It replaces the compile-time pin asserts the per-board builds had: the pins are no
# longer known at compile time, since one image serves every board on a chip.

idf_component_register(
  SRCS          "src/BoardChecks.cpp"
  INCLUDE_DIRS  "include"
  REQUIRES      common          # Common.h, BoardDescriptor.h
                esp_driver_gpio # driver/gpio.h
  PRIV_REQUIRES logging         # OS_LOG* in BoardChecks.cpp
)
