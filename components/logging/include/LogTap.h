#pragma once

#include <cstddef>

// A second consumer of every OS_LOG* / OS_PANIC line, next to the serial console. Diagnostics installs one to keep
// error lines for upload and, when built with SD card support, to queue every record for the card.
namespace OpenShock::Logging {
  /// @brief Receives one log record: its severity letter (V, D, I, W, E, N, or C for a panic), its tag and the
  ///        formatted message (no prefix, no line ending, not NUL-terminated). Runs on the logging task, after the line
  ///        went to the console. It must not log (that would recurse) or block: it is on every logging call's path.
  using LogTap = void (*)(char level, const char* tag, const char* message, std::size_t len);

  /// @brief Installs the tap, replacing any previous one; nullptr removes it.
  void SetTap(LogTap tap);
}  // namespace OpenShock::Logging
