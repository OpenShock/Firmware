#pragma once

#include <driver/uart.h>

// Use the same UART that your console / printf uses
// On most boards this is UART_NUM_0
#define OPENSHOCK_UART = UART_NUM_0;

namespace OpenShock {
  // Call this once during startup (instead of Serial.begin)
  inline void SerialInit()
  {
    // If you already use esp_console, you might already have
    // the driver installed. Then you can skip the install part.
    uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity  = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(OPENSHOCK_UART, &uart_config));

    // RX buffer only; TX is handled by vfs/printf
    ESP_ERROR_CHECK(uart_driver_install(OPENSHOCK_UART, 2048, 0, 0, nullptr, 0));

    // Route stdin/stdout to this UART so printf/scanf work
    esp_vfs_dev_uart_use_driver(OPENSHOCK_UART);
  }

  // Arduino-like helpers
  inline int SerialAvailable()
  {
    size_t len = 0;
    uart_get_buffered_data_len(OPENSHOCK_UART, &len);
    return static_cast<int>(len);
  }

  inline int SerialRead()
  {
    std::uint8_t c;
    // timeout = 0 ticks → non-blocking
    int n = uart_read_bytes(OPENSHOCK_UART, &c, 1, 0);
    if (n == 1) {
      return static_cast<int>(c);
    }
    return -1; // no data
  }
}  // namespace OpenShock