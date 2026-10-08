# OpenShock LED drivers — both mono (PWM via LEDC) and RGB (WS2812B via the
# ESP-IDF RMT driver, esp_driver_rmt).
#
# Public surface:
#   led_drivers/MonoLedDriver.h
#   led_drivers/RgbLedDriver.h
#
# Note: this is driver-level only. The VisualStateManager that picks patterns
# based on estop/gateway/wifi state is a separate concern and lives in main.

idf_component_register(
  SRCS          "src/MonoLedDriver.cpp"
                "src/RgbLedDriver.cpp"
  INCLUDE_DIRS  "include"
  REQUIRES      common                       # Common.h, SimpleMutex in public headers
                freertos                     # TaskHandle_t in public headers
                esp_driver_rmt               # driver/rmt_tx.h, rmt_encoder.h in RgbLedDriver.h
  PRIV_REQUIRES chipset                      # IsValidOutputPin
                logging                      # OS_LOG*
                esp_driver_ledc              # driver/ledc.h (mono PWM)   # FnProxy/TaskUtils via common
)
