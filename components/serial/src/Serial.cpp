#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "serial/Serial.h"

#include <sdkconfig.h>

#include <esp_err.h>
#include <esp_rom_serial_output.h>

#include <algorithm>
#include <atomic>
#include <cstdio>

// Select the console backend from the configured primary console.
#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
#define OS_CONSOLE_USJ 1
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
// ROM USB-OTG CDC console (ESP32-S2, which has no USB-Serial-JTAG). Driven through
// the low-level console API rather than the cdcacm VFS, which would rewrite the
// "\r\n" our log lines already carry into "\r\r\n"; UART and USJ write raw too.
#define OS_CONSOLE_CDC 1
#include <esp_private/usb_console.h>
#elif defined(CONFIG_ESP_CONSOLE_UART) || defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
#define OS_CONSOLE_UART 1
#include <driver/uart.h>
#include <driver/uart_vfs.h>
#endif

using namespace OpenShock;

static constexpr int k_consoleBufferSize = 256;

// Published with release ordering after s_writeMutex is created, so a writer on another core that sees it set also
// sees the mutex.
static std::atomic<bool> s_initialized = false;

// Serializes writers (the logger and the console) so one Write() never
// interleaves with another, even when it is split into several driver calls.
static StaticSemaphore_t s_writeMutexStorage;
static SemaphoreHandle_t s_writeMutex = nullptr;

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
  // With a TX ring buffer, uart_write_bytes returns once the bytes are queued instead of waiting ~9 ms per 100-byte
  // line at 115200 baud, so logging no longer stalls the caller (event loop, httpd, ...). OS_PANIC waits 5 s before
  // restarting, which lets the buffer drain; only OS_PANIC_INSTANT can lose its last line.
  static constexpr int k_uartTxBufferSize = 2048;
  esp_err_t err                            = uart_driver_install(static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM), k_consoleBufferSize, k_uartTxBufferSize, 0, nullptr, 0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    return false;
  }
  uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
#endif
  // OS_CONSOLE_CDC: the startup code already brought the ROM CDC console up.

  s_writeMutex = xSemaphoreCreateMutexStatic(&s_writeMutexStorage);

  // OpenShock's own output goes through Write(); keep stdout unbuffered so anything
  // still printing through C stdio (e.g. ESP-IDF's logs) isn't held back either.
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  s_initialized.store(true, std::memory_order_release);
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
#elif defined(OS_CONSOLE_CDC)
  int read = static_cast<int>(esp_usb_console_read_buf(reinterpret_cast<char*>(buffer), len));
#else
  int read = 0;
#endif

  return read < 0 ? 0 : read;
}

// Writes to the installed console driver. Called with s_writeMutex held.
static int driverWrite(const uint8_t* data, std::size_t len)
{
#if defined(OS_CONSOLE_USJ)
  // The driver queues each call into its TX ring buffer as a single item and rejects
  // anything larger than the buffer outright, so feed it buffer-sized chunks.
  std::size_t sent = 0;
  while (sent < len) {
    std::size_t chunk = std::min(len - sent, static_cast<std::size_t>(k_consoleBufferSize));
    int n             = usb_serial_jtag_write_bytes(data + sent, chunk, pdMS_TO_TICKS(50));
    if (n <= 0) {
      break;
    }
    sent += static_cast<std::size_t>(n);
  }
  return static_cast<int>(sent);
#elif defined(OS_CONSOLE_UART)
  return uart_write_bytes(static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM), data, len);
#elif defined(OS_CONSOLE_CDC)
  // write_buf never blocks: it takes what fits in its TX buffer and returns 0 while the
  // host isn't draining it. Retry for up to 50 ms (as the USJ path waits), then flush so
  // prompts without a trailing newline go out too.
  // Elapsed-time check: comparing against an absolute deadline breaks when the tick counter wraps.
  std::size_t sent   = 0;
  TickType_t started = xTaskGetTickCount();
  while (sent < len) {
    ssize_t n = esp_usb_console_write_buf(reinterpret_cast<const char*>(data + sent), len - sent);
    if (n < 0) {
      break;
    }
    if (n == 0) {
      if (xTaskGetTickCount() - started >= pdMS_TO_TICKS(50)) {
        break;
      }
      vTaskDelay(1);
      continue;
    }
    sent += static_cast<std::size_t>(n);
  }
  esp_usb_console_flush();
  return static_cast<int>(sent);
#else
  romWrite(data, len);
  return static_cast<int>(len);
#endif
}

int Serial::Write(const uint8_t* data, std::size_t len)
{
  if (data == nullptr || len == 0) {
    return 0;
  }

  if (!s_initialized.load(std::memory_order_acquire)) {
    romWrite(data, len);
    return static_cast<int>(len);
  }

  xSemaphoreTake(s_writeMutex, portMAX_DELAY);
  int written = driverWrite(data, len);
  xSemaphoreGive(s_writeMutex);

  return written < 0 ? 0 : written;
}
