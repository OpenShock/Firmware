#pragma once

#include <driver/gpio.h>

namespace OpenShock {
  constexpr bool IsValidGPIOPin(std::uint8_t pin) {
    if (pin >= GPIO_NUM_MAX) {
      return false;
    }

    // Normally used for UART0 TXD and RXD.
    // See: ESP32 Series Datasheet Version 4.3 Section 2.2 Pin Overview
    if (pin == GPIO_NUM_1 || pin == GPIO_NUM_3) {
      return false;
    }

    // Used to control the boot mode of the chip.
    // See: ESP32 Series Datasheet Version 4.3 Section 2.4 Strapping Pins
    if (pin == GPIO_NUM_0 || pin == GPIO_NUM_2) {
      return false;
    }

    // Note: 5 and 15 are used for SDIO slave timing selection, but 15 is already widely used for RF, so we'll let these slide.

    // Allocated for communication with in-package flash/PSRAM and NOT recommended for other uses.
    // See: ESP32 Series Datasheet Version 4.3 Section 2.2.1 Restrictions for GPIOs and RTC_GPIOs
    if (pin == GPIO_NUM_6 || pin == GPIO_NUM_7 || pin == GPIO_NUM_8 || pin == GPIO_NUM_9 || pin == GPIO_NUM_10 || pin == GPIO_NUM_11 || pin == GPIO_NUM_16 || pin == GPIO_NUM_17) {
      return false;
    }

    return true;
  }
  constexpr bool IsValidInputPin(std::uint8_t pin) {
    if (!IsValidGPIOPin(pin)) {
      return false;
    }

    return GPIO_IS_VALID_GPIO(pin) && !GPIO_IS_VALID_OUTPUT_GPIO(pin);
  }
  constexpr bool IsValidOutputPin(std::uint8_t pin) {
    if (!IsValidGPIOPin(pin)) {
      return false;
    }

    return GPIO_IS_VALID_OUTPUT_GPIO(pin);
  }
}  // namespace OpenShock
