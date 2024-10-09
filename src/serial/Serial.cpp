#include <freertos/FreeRTOS.h>

#include "serial/Serial.h"

#include <driver/uart.h>
#include <freertos/task.h>

#define TAG "serial::Serial"

#define UART_NUM UART_NUM_0
#define UART_TXP 1
#define UART_RXP 3
#define UART_BUFFER_SIZE 1024
#define UART_QUEUE_SIZE 10
#define UART_BAUD_RATE 115200

using namespace OpenShock;

bool Serial::Init() {
  if (uart_is_driver_installed(UART_NUM)) {
    return true;
  }

  // Configure the UART
  uart_config_t uart_config = {
    .baud_rate = UART_BAUD_RATE,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT,
  }; // default values

  esp_err_t err;

  err = uart_driver_install(UART_NUM, UART_BUFFER_SIZE, UART_BUFFER_SIZE, UART_QUEUE_SIZE, nullptr, 0);
  if (err != ESP_OK) {
    return false;
  }

  err = uart_param_config(UART_NUM, &uart_config);
  if (err != ESP_OK) {
    return false;
  }

  err = uart_set_pin(UART_NUM, UART_TXP, UART_RXP, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (err != ESP_OK) {
    return false;
  }

  uart_

  return true;
}
