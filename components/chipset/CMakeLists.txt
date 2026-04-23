# OpenShock chipset utilities — GPIO validation, compile-time pin assertions.
#
# Public surface:
#   Chipset.h — OpenShock::IsValidInputPin / IsValidOutputPin per IDF_TARGET
#
# CompatibilityChecks.cpp contains static_asserts that fail the build if any
# board config (OPENSHOCK_ESTOP_PIN, OPENSHOCK_RF_TX_GPIO, etc.) resolves to
# an invalid pin for the current chip. It has to be in the link for the
# asserts to run; no runtime code.

idf_component_register(
  SRCS         "src/CompatibilityChecks.cpp"
  INCLUDE_DIRS "include"
  REQUIRES     common      # Common.h
               driver      # driver/gpio.h
)
