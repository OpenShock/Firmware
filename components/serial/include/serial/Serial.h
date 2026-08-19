#pragma once

#include <cstddef>
#include <cstdint>

// Low-level serial transport for the device console (UART0 or USB-Serial-JTAG,
// selected by the ESP-IDF console sdkconfig). This is a foundation-layer
// component: it owns the raw byte I/O to the port and depends on nothing above
// the IDF drivers, so `logging` can write through it without a dependency cycle.
namespace OpenShock::Serial {
  /**
   * @brief Installs the console driver (UART0 / USB-Serial-JTAG) and routes stdout
   *        through it, unbuffered so interactive prompts flush immediately.
   *        Idempotent.
   *
   * @return true on success, false if the driver could not be installed.
   */
  [[nodiscard]] bool Init();

  /**
   * @brief Non-blocking read of up to @p len bytes from the console.
   *
   * @param buffer Destination buffer.
   * @param len    Maximum number of bytes to read.
   * @return Number of bytes read (0 if none available).
   */
  int Read(uint8_t* buffer, std::size_t len);

  /**
   * @brief Writes @p len raw bytes to the console. Before Init() the bytes go
   *        straight to the ROM serial output (early-boot logging), never C stdio.
   *
   * @param data Source buffer.
   * @param len  Number of bytes to write.
   * @return Number of bytes written.
   */
  int Write(const uint8_t* data, std::size_t len);
}  // namespace OpenShock::Serial
