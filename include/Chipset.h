#pragma once

#include "Common.h"

#include <driver/gpio.h>

#include <bitset>

// The following chipsets are supported by the OpenShock firmware.
// To find documentation for a specific chipset, see the docs link.
// You need to navigate to the datasheets pin's section, the unsafe pins are usually listed under the "Strapping Pins" section.
// We want to avoid using these pins as they are used for boot mode and SDIO slave timing selection, and its easy to encounter issues if we use them.
#pragma region Chipset Definitions
// ESP8266EX
//
// Chips: ESP8266EX
//
// Docs: https://www.espressif.com/sites/default/files/documentation/0a-esp8266ex_datasheet_en.pdf
#ifdef OPENSHOCK_FW_CHIP_ESP8266EX
#define OPENSHOCK_FW_CHIP_DEFINED
// GPIO3, GPIO1 is used for UART0 RXD/TXD.
// GPIO0, GPIO2, and GPIO15 are used for boot mode and SDIO slave timing selection.
#define CHIP_UNSAFE_GPIO(pin) ((pin) == GPIO_NUM_0 || (pin) == GPIO_NUM_1 || (pin) == GPIO_NUM_2 || (pin) == GPIO_NUM_3 || (pin) == GPIO_NUM_15)
#endif

// ESP8285
//
// Chips: ESP8285N08, ESP8285H16
//
// Docs: https://www.espressif.com/sites/default/files/documentation/0a-esp8285_datasheet_en.pdf
#ifdef OPENSHOCK_FW_CHIP_ESP8285
#define OPENSHOCK_FW_CHIP_DEFINED
// GPIO3, GPIO1 is used for UART0 RXD/TXD.
// GPIO0, GPIO2, and GPIO15 are used for boot mode and SDIO slave timing selection.
#define CHIP_UNSAFE_GPIO(pin) ((pin) == GPIO_NUM_0 || (pin) == GPIO_NUM_1 || (pin) == GPIO_NUM_2 || (pin) == GPIO_NUM_3 || (pin) == GPIO_NUM_15)
#endif

// ESP8684
//
// Chips: ESP8684H1, ESP8684H2, ESP8684H4
//
// Docs: https://www.espressif.com/sites/default/files/documentation/esp8684_datasheet_en.pdf
// Docs: https://www.espressif.com/sites/default/files/documentation/esp8684_technical_reference_manual_en.pdf#bootctrl
#ifdef OPENSHOCK_FW_CHIP_ESP8684
#define OPENSHOCK_FW_CHIP_DEFINED
// GPIO3, GPIO1 is used for UART0 RXD/TXD.
// GPIO8, GPIO9 are strapping pins used to control the boot mode adn ROM code printing
#define CHIP_UNSAFE_GPIO(pin) ((pin) == GPIO_NUM_1 || (pin) == GPIO_NUM_3 || (pin) == GPIO_NUM_8 || (pin) == GPIO_NUM_9)
#endif

// ESP8685
//
// Chips: ESP8685H2, ESP8685H4
//
// Docs: https://www.espressif.com/sites/default/files/documentation/esp8685_datasheet_en.pdf
#ifdef OPENSHOCK_FW_CHIP_ESP8685
#define OPENSHOCK_FW_CHIP_DEFINED
// GPIO20, GPIO21 is used for UART0 RXD/TXD.
// GPIO2, GPIO8, GPIO9 are strapping pins.
#define CHIP_UNSAFE_GPIO(pin) ((pin) == GPIO_NUM_2 || (pin) == GPIO_NUM_8 || (pin) == GPIO_NUM_9 || (pin) == GPIO_NUM_20 || (pin) == GPIO_NUM_21)
#endif

// ESP32
//
// Chips: ESP32-D0WD-V3, ESP32-D0WDR2-V3, ESP32-U4WDH, ESP32-S0WD, ESP32-D0WD, ESP32-D0WDQ6, ESP32-D0WDQ6-V3
//
// Docs: https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf
#ifdef OPENSHOCK_FW_CHIP_ESP32
#define OPENSHOCK_FW_CHIP_DEFINED

// GPIO1, GPIO3 is used for UART0 RXD/TXD.
// GPIO0, GPIO2 is used to control the boot mode of the chip.
// GPIO5, GPIO15 is used for SDIO slave timing selection.
// GPIO6, GPIO7, GPIO8, GPIO9, GPIO11, GPIO16, GPIO17 is used for SPI flash connection. (DO NOT TOUCH)
//
// See: ESP32 Series Datasheet Version 4.3 Section 2.2 Pin Overview
// See: ESP32 Series Datasheet Version 4.3 Section 2.4 Strapping Pins
#define CHIP_UNSAFE_GPIO(pin)                                                                                                                                                                                                                                  \
  ((pin) == GPIO_NUM_1 || (pin) == GPIO_NUM_3 || (pin) == GPIO_NUM_0 || (pin) == GPIO_NUM_2 || (pin) == GPIO_NUM_5 || (pin) == GPIO_NUM_15 || (pin) == GPIO_NUM_6 || (pin) == GPIO_NUM_7 || (pin) == GPIO_NUM_8 || (pin) == GPIO_NUM_9 || (pin) == GPIO_NUM_11 || (pin) == GPIO_NUM_16 \
   || (pin) == GPIO_NUM_17)
#endif

// ESP32-PICO-D4
//
// Chips: ESP32-PICO-D4
//
// Docs: https://www.espressif.com/sites/default/files/documentation/esp32-pico_series_datasheet_en.pdf (Section 2.1.2 - 2.1.3 Pin Description and Pin Mapping between ESP and Flash/PSRAM)
#ifdef OPENSHOCK_FW_CHIP_ESP32PICOD4
#define OPENSHOCK_FW_CHIP_DEFINED
// GPIO3, GPIO1 is used for UART0 RXD/TXD.
// GPIO25, GPIO27, GPIO29, GPIO30, GPIO31, GPIO32, GPIO33 is used for SPI flash connection. (DO NOT TOUCH)
// GPIO12, GPIO0, GPIO2, GPIO15, and GPIO5 are used for boot mode and SDIO slave timing selection.
#define CHIP_UNSAFE_GPIO(pin) ((pin) == GPIO_NUM_3 || (pin) == GPIO_NUM_1 || (pin) == GPIO_NUM_25 || (pin) == GPIO_NUM_27 || (pin) == GPIO_NUM_29 || (pin) == GPIO_NUM_30 || (pin) == GPIO_NUM_31 || (pin) == GPIO_NUM_32 || (pin) == GPIO_NUM_33 || (pin) == GPIO_NUM_12 || (pin) == GPIO_NUM_0 || (pin) == GPIO_NUM_2 || (pin) == GPIO_NUM_15 || (pin) == GPIO_NUM_5)
#endif

// ESP32-PICO-V3
//
// Chips:, ESP32-PICO-V3, ESP32-PICO-V3-02
//
// Docs: https://www.espressif.com/sites/default/files/documentation/esp32-pico_series_datasheet_en.pdf
#ifdef OPENSHOCK_FW_CHIP_ESP32PICOV3
#define OPENSHOCK_FW_CHIP_DEFINED
// GPIO3, GPIO1 is used for UART0 RXD/TXD.
// GPIO6, GPIO11, GPIO9, GPIO10 is used for SPI flash connection. (DO NOT TOUCH)
// GPIO12, GPIO0, GPIO2, GPIO15, and GPIO5 are used for boot mode and SDIO slave timing selection.
#define CHIP_UNSAFE_GPIO(pin) ((pin) == GPIO_NUM_3 || (pin) == GPIO_NUM_1 || (pin) == GPIO_NUM_6 || (pin) == GPIO_NUM_11 || (pin) == GPIO_NUM_9 || (pin) == GPIO_NUM_10 || (pin) == GPIO_NUM_12 || (pin) == GPIO_NUM_0 || (pin) == GPIO_NUM_2 || (pin) == GPIO_NUM_15 || (pin) == GPIO_NUM_5)
#endif

// ESP32-S2
//
// Chips: ESP32-S2, ESP32-S2FH2, ESP32-S2FH4, ESP32-S2FN4R2, ESP32-S2R2
//
// Docs: https://www.espressif.com/sites/default/files/documentation/esp32-s2_datasheet_en.pdf
#ifdef OPENSHOCK_FW_CHIP_ESP32S2
#define OPENSHOCK_FW_CHIP_DEFINED
// GPIO44, GPIO43 is used for UART0 RXD/TXD.
// GPIO29, GPIO26, GPIO32, GPIO31, GPIO30, GPIO28, GPIO27 is used for SPI flash connection. (DO NOT TOUCH)
// GPIO0, GPIO45, GPIO46 is strapping pins used to control the boot mode and misc. functions.
#define CHIP_UNSAFE_GPIO(pin) ((pin) == GPIO_NUM_44 || (pin) == GPIO_NUM_43 || (pin) == GPIO_NUM_29 || (pin) == GPIO_NUM_26 || (pin) == GPIO_NUM_32 || (pin) == GPIO_NUM_31 || (pin) == GPIO_NUM_30 || (pin) == GPIO_NUM_28 || (pin) == GPIO_NUM_27 || (pin) == GPIO_NUM_0 || (pin) == GPIO_NUM_45 || (pin) == GPIO_NUM_46)
#endif

// ESP32-S3
//
// Chips: ESP32-S3, ESP32-S3FN8, ESP32-S3R2, ESP32-S3R8, ESP32-S3R8V, ESP32-S3R16V, ESP32-S3FH4R2
//
// Docs: https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
#ifdef OPENSHOCK_FW_CHIP_ESP32S3
#define OPENSHOCK_FW_CHIP_DEFINED
// GPIO44, GPIO43 is used for UART0 RXD/TXD.
// GPIO19, GPIO20 is used for USB serial, flashing, and debugging.
// GPIO30, GPIO29, GPIO26, GPIO32, GPIO31, GPIO28, GPIO27, GPIO33, GPIO34, GPIO35, GPIO36, GPIO37 is used for SPI flash connection. (DO NOT TOUCH)
// GPIO0, GPIO3, GPIO45, GPIO46 is strapping pins used to control the boot mode and misc. functions.
#define CHIP_UNSAFE_GPIO(pin) ((pin) == GPIO_NUM_44 || (pin) == GPIO_NUM_43 || (pin) == GPIO_NUM_19 || (pin) == GPIO_NUM_20 || (pin) == GPIO_NUM_30 || (pin) == GPIO_NUM_29 || (pin) == GPIO_NUM_26 || (pin) == GPIO_NUM_32 || (pin) == GPIO_NUM_31 || (pin) == GPIO_NUM_28 || (pin) == GPIO_NUM_27 || (pin) == GPIO_NUM_33 || (pin) == GPIO_NUM_34 || (pin) == GPIO_NUM_35 || (pin) == GPIO_NUM_36 || (pin) == GPIO_NUM_37 || (pin) == GPIO_NUM_0 || (pin) == GPIO_NUM_3 || (pin) == GPIO_NUM_45 || (pin) == GPIO_NUM_46)
#endif

// ESP32-S3-PICO-1
//
// Chips: ESP32-S3-PICO-1-N8R2, ESP32-S3-PICO-1-N8R8
//
// Docs: https://www.espressif.com/sites/default/files/documentation/esp32-s3-pico-1_datasheet_en.pdf
#ifdef OPENSHOCK_FW_CHIP_ESP32S3PICO1
#define OPENSHOCK_FW_CHIP_DEFINED
#error "ESP32-S3-PICO-1 is not supported yet."
#endif

// ESP32-C3
//
// Chips: ESP32-C3, ESP32-C3FN4, ESP32-C3FH4, ESP32-C3FH4AZ
//
// Docs: https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf
#ifdef OPENSHOCK_FW_CHIP_ESP32C3
#define OPENSHOCK_FW_CHIP_DEFINED
// GPIO20, GPIO21 is used for UART0 RXD/TXD.
// GPIO18, GPIO19, GPIO4, GPIO5, GPIO6, GPIO7 is used for USB serial, flashing, and debugging.
// GPIO12, GPIO13, GPIO14, GPIO15, GPIO16, GPIO17 is used for SPI flash connection. (DO NOT TOUCH)
// GPIO2, GPIO8, GPIO9 is strapping pins used to control the boot mode and misc. functions.
#define CHIP_UNSAFE_GPIO(pin) ((pin) == GPIO_NUM_20 || (pin) == GPIO_NUM_21 || (pin) == GPIO_NUM_18 || (pin) == GPIO_NUM_19 || (pin) == GPIO_NUM_4 || (pin) == GPIO_NUM_5 || (pin) == GPIO_NUM_6 || (pin) == GPIO_NUM_7 || (pin) == GPIO_NUM_12 || (pin) == GPIO_NUM_13 || (pin) == GPIO_NUM_14 || (pin) == GPIO_NUM_15 || (pin) == GPIO_NUM_16 || (pin) == GPIO_NUM_17 || (pin) == GPIO_NUM_2 || (pin) == GPIO_NUM_8 || (pin) == GPIO_NUM_9)
#endif

// ESP32-C6
//
// Chips: ESP32-C6, ESP32-C6FH4
//
// Docs: https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf
#ifdef OPENSHOCK_FW_CHIP_ESP32C6
#define OPENSHOCK_FW_CHIP_DEFINED
#error "ESP32-C6 is not supported yet."
#endif

#ifndef OPENSHOCK_FW_CHIP_DEFINED
#error "Selected chipset is misspelled or not supported by OpenShock."
#endif

#pragma endregion

/// Board specific bad-pin bypasses for compatibility reasons.
/// To be clear, these pins are still unsafe, but we need to use them for compatibility reasons.
#pragma region Board Specific Bypasses

#ifdef OPENSHOCK_FW_BOARD_WEMOSD1MINIESP32
#define OPENSHOCK_BYPASSED_GPIO(pin) ((pin) == 15 || (pin) == 2)
#endif
#ifdef OPENSHOCK_FW_BOARD_WEMOSLOLINS2MINI
#define OPENSHOCK_BYPASSED_GPIO(pin) ((pin) == 15)
#endif
#ifdef OPENSHOCK_FW_BOARD_PISHOCK2023
#define OPENSHOCK_BYPASSED_GPIO(pin) ((pin) == 12 || (pin) == 2)
#endif
#ifdef OPENSHOCK_FW_BOARD_PISHOCKLITE2021
#define OPENSHOCK_BYPASSED_GPIO(pin) ((pin) == 15 || (pin) == 2)
#endif
#ifdef OPENSHOCK_FW_BOARD_OPENSHOCKCOREV1
#define OPENSHOCK_BYPASSED_GPIO(pin) ((pin) == 15 || (pin) == 35)
#endif
#ifdef OPENSHOCK_FW_BOARD_DFROBOTFIREBEETLE2ESP32E
#define OPENSHOCK_BYPASSED_GPIO(pin) ((pin) == 2 || (pin) == 5)
#endif
#ifndef OPENSHOCK_BYPASSED_GPIO
#define OPENSHOCK_BYPASSED_GPIO(pin) (false)
#endif

#pragma endregion

namespace OpenShock {
  constexpr bool IsValidGPIOPin(uint8_t pin) {
    if (pin == OPENSHOCK_GPIO_INVALID) {
      return false;
    }

    if (pin >= GPIO_NUM_MAX) {
      return false;
    }

    if (!GPIO_IS_VALID_GPIO(pin)) {
      return false;
    }

    if (OPENSHOCK_BYPASSED_GPIO(pin)) {
      return true;
    }

    if (CHIP_UNSAFE_GPIO(pin)) {
      return false;
    }

    return true;
  }
  constexpr bool IsValidInputPin(uint8_t pin) {
    return IsValidGPIOPin(pin);
  }
  constexpr bool IsValidOutputPin(uint8_t pin) {
    if (!IsValidGPIOPin(pin)) {
      return false;
    }

    if (!GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
      return false;
    }

    return true;
  }

  const std::size_t GPIOPinSetSize = GPIO_NUM_MAX + 1;
  typedef std::bitset<GPIOPinSetSize> GPIOPinSet;

  constexpr GPIOPinSet GetValidGPIOPins() {
    GPIOPinSet pins;
    for (std::size_t i = 0; i < GPIOPinSetSize; i++) {
      if (IsValidGPIOPin(i)) {
        pins.set(i);
      }
    }
    return pins;
  }
  constexpr GPIOPinSet GetValidInputPins() {
    GPIOPinSet pins;
    for (std::size_t i = 0; i < GPIOPinSetSize; i++) {
      if (IsValidInputPin(i)) {
        pins.set(i);
      }
    }
    return pins;
  }
  constexpr GPIOPinSet GetValidOutputPins() {
    GPIOPinSet pins;
    for (std::size_t i = 0; i < GPIOPinSetSize; i++) {
      if (IsValidOutputPin(i)) {
        pins.set(i);
      }
    }
    return pins;
  }
  inline std::vector<uint8_t> GetValidInputPinsVector() {
    std::vector<uint8_t> pins(GPIOPinSetSize);
    for (std::size_t i = 0; i < GPIOPinSetSize; i++) {
      if (IsValidInputPin(i)) {
        pins.push_back(i);
      }
    }
    pins.shrink_to_fit();
    return pins;
  }
  inline std::vector<uint8_t> GetValidOutputPinsVector() {
    std::vector<uint8_t> pins(GPIOPinSetSize);
    for (std::size_t i = 0; i < GPIOPinSetSize; i++) {
      if (IsValidOutputPin(i)) {
        pins.push_back(i);
      }
    }
    pins.shrink_to_fit();
    return pins;
  }
}  // namespace OpenShock
