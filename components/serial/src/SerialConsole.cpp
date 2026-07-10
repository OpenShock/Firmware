#include "serial/SerialConsole.h"

const char* const TAG = "SerialConsole";

#include "Logging.h"

#include <sdkconfig.h>

#include <esp_err.h>

#include <cstdio>

// Select the RX backend from the configured primary console. TX always goes to
// stdout (the IDF console handles primary + any secondary channel). USB-CDC
// console (native TinyUSB) is not handled here yet; RX is disabled in that case.
#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
#define OS_CONSOLE_USJ 1
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#elif defined(CONFIG_ESP_CONSOLE_UART) || defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
#define OS_CONSOLE_UART 1
#include <driver/uart.h>
#include <driver/uart_vfs.h>
#endif

using namespace OpenShock;

static constexpr int k_consoleRxBufferSize = 256;

static bool s_initialized = false;

bool SerialConsole::Init()
{
  if (s_initialized) {
    return true;
  }

#if defined(OS_CONSOLE_USJ)
  usb_serial_jtag_driver_config_t cfg = {.tx_buffer_size = k_consoleRxBufferSize, .rx_buffer_size = k_consoleRxBufferSize};

  esp_err_t err = usb_serial_jtag_driver_install(&cfg);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    OS_LOGE(TAG, "Failed to install USB-Serial-JTAG driver: %s", esp_err_to_name(err));
    return false;
  }
  usb_serial_jtag_vfs_use_driver();
#elif defined(OS_CONSOLE_UART)
  esp_err_t err = uart_driver_install(static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM), k_consoleRxBufferSize, 0, 0, nullptr, 0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    OS_LOGE(TAG, "Failed to install console UART driver: %s", esp_err_to_name(err));
    return false;
  }
  uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
#else
  OS_LOGW(TAG, "No supported console configured for RX; serial command input is disabled");
#endif

  // The command handler echoes prompts without trailing newlines, so make stdout
  // unbuffered to flush them immediately.
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  s_initialized = true;
  return true;
}

int SerialConsole::Read(uint8_t* buffer, std::size_t len)
{
  if (buffer == nullptr || len == 0) {
    return 0;
  }

#if defined(OS_CONSOLE_USJ)
  int read = usb_serial_jtag_read_bytes(buffer, static_cast<uint32_t>(len), 0);
#elif defined(OS_CONSOLE_UART)
  int read = uart_read_bytes(static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM), buffer, static_cast<uint32_t>(len), 0);
#else
  int read = 0;
#endif

  return read < 0 ? 0 : read;
}
