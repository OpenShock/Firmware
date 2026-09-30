#include <freertos/FreeRTOS.h>

#include "serial/Serial.h"

#include <sdkconfig.h>

#include <esp_err.h>
#include <esp_rom_serial_output.h>

#include <cstdio>

// Select the console backend from the configured primary console. USB-CDC console
// (native TinyUSB) is not handled here yet; I/O falls back to the ROM output.
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

static constexpr int k_consoleBufferSize = 256;

static bool s_initialized = false;

// Byte-for-byte write to the ROM serial output. Used before the driver is
// installed (early-boot logging) or when no console driver is configured. This
// is a raw serial write, not C stdio.
static void romWrite(const uint8_t* data, std::size_t len)
{
  for (std::size_t i = 0; i < len; i++) {
    esp_rom_output_putc(static_cast<char>(data[i]));
  }
}

bool Serial::Init()
{
  if (s_initialized) {
    return true;
  }

#if defined(OS_CONSOLE_USJ)
  usb_serial_jtag_driver_config_t cfg = {.tx_buffer_size = k_consoleBufferSize, .rx_buffer_size = k_consoleBufferSize};

  esp_err_t err = usb_serial_jtag_driver_install(&cfg);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return false;
  }
  usb_serial_jtag_vfs_use_driver();
#elif defined(OS_CONSOLE_UART)
  // TX buffer 0: uart_write_bytes blocks until the bytes are on the wire, so log
  // lines (incl. a final panic message before reset) are flushed synchronously.
  esp_err_t err = uart_driver_install(static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM), k_consoleBufferSize, 0, 0, nullptr, 0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return false;
  }
  uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
#endif

  // The command handler echoes prompts without trailing newlines, so make stdout
  // unbuffered to flush them immediately.
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  s_initialized = true;
  return true;
}

int Serial::Read(uint8_t* buffer, std::size_t len)
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

int Serial::Write(const uint8_t* data, std::size_t len)
{
  if (data == nullptr || len == 0) {
    return 0;
  }

  if (!s_initialized) {
    romWrite(data, len);
    return static_cast<int>(len);
  }

#if defined(OS_CONSOLE_USJ)
  int written = usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(50));
#elif defined(OS_CONSOLE_UART)
  int written = uart_write_bytes(static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM), data, len);
#else
  romWrite(data, len);
  int written = static_cast<int>(len);
#endif

  return written < 0 ? 0 : written;
}
