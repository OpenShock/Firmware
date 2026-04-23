# OpenShock LED drivers — both mono (PWM via LEDC) and RGB (WS2812B via
# Arduino-ESP32's RMT HAL).
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
  REQUIRES      common                       # Common.h in public headers
                concurrency                  # SimpleMutex in public headers
                freertos                     # TaskHandle_t in public headers
                framework-arduinoespressif32 # esp32-hal-rmt.h in RgbLedDriver.h
  PRIV_REQUIRES chipset                      # IsValidOutputPin
                logging                      # OS_LOG*
                util                         # FnProxy, TaskUtils
                esp_driver_ledc              # driver/ledc.h (mono PWM)
)
