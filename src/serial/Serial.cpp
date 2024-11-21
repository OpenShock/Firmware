#include <freertos/FreeRTOS.h>

#include "serial/Serial.h"

#include <driver/uart.h>
#include <driver/usb_serial_jtag.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#define TAG "serial::Serial"

#define USE_USB_JTAG false

#define UART_NUM        UART_NUM_2  // Only for ESP32 and ESP32S3
#define UART_TXP        4
#define UART_RXP        5
#define UART_QUEUE_SIZE 10
#define UART_BAUD_RATE  115'200

#define BUFFER_SIZE 1024

using namespace OpenShock;

bool Serial::Init()
{
  esp_err_t err;
#if USE_USB_JTAG
  usb_serial_jtag_driver_config_t usb_serial_jtag_config = {
    .tx_buffer_size = BUFFER_SIZE,
    .rx_buffer_size = BUFFER_SIZE,
  };

  err = usb_serial_jtag_driver_install(&usb_serial_jtag_config);
  if (err != ESP_OK) {
    return false;
  }
#else
  if (uart_is_driver_installed(UART_NUM)) {
    return true;
  }

  // Configure the UART
  uart_config_t uart_config = {
    .baud_rate = UART_BAUD_RATE,
    .data_bits = UART_DATA_8_BITS,
    .parity    = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
#ifdef UART_SCLK_DEFAULT
    .source_clk = UART_SCLK_REF_TICK,
#elif SOC_UART_SUPPORT_REF_TICK
    .source_clk = UART_SCLK_REF_TICK,
#else
    .source_clk = UART_SCLK_APB,
#endif
  };  // default values

  err = uart_driver_install(UART_NUM, BUFFER_SIZE, BUFFER_SIZE, UART_QUEUE_SIZE, nullptr, 0);
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
#endif

  return true;
}
