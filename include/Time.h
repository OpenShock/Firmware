#pragma once

#include <esp_timer.h>

#include <cstdint>

namespace OpenShock {
  /**
   * @brief Returns the current time in microseconds
   *
   * @return std::int64_t The current time in microseconds
   *
   * @note This function overflows after 292471 years
   */
  inline std::int64_t micros() {
    return esp_timer_get_time();
  }

  /**
   * @brief Returns the current time in milliseconds
   *
   * @return std::int64_t The current time in milliseconds
   *
   * @note This function overflows after 292471208 years
   */
  inline std::int64_t millis() {
    return esp_timer_get_time() / 1000LL;
  }
}
