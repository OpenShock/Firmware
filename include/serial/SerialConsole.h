#pragma once

#include <cstddef>
#include <cstdint>

namespace OpenShock::SerialConsole {
  /**
   * @brief Installs the console RX driver (UART0 / USB-Serial-JTAG depending on the
   *        configured console) and switches stdout to unbuffered so interactive
   *        prompts flush immediately. Idempotent.
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
}  // namespace OpenShock::SerialConsole
