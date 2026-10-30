#pragma once

#include "StringHelpers.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace OpenShock {
  /// @brief The least severe log level a hub uploads; None uploads no logs. Same values as the schema's enum.
  enum class DiagnosticsLogLevel : uint8_t {
    None,
    Error,
    Warn,
    Info,
    Debug,
    Verbose
  };

  [[nodiscard]] inline const char* DiagnosticsLogLevelName(DiagnosticsLogLevel level)
  {
    switch (level) {
      case DiagnosticsLogLevel::None:
        return "none";
      case DiagnosticsLogLevel::Error:
        return "error";
      case DiagnosticsLogLevel::Warn:
        return "warn";
      case DiagnosticsLogLevel::Info:
        return "info";
      case DiagnosticsLogLevel::Debug:
        return "debug";
      case DiagnosticsLogLevel::Verbose:
        return "verbose";
      default:
        return "unknown";
    }
  }

  [[nodiscard]] inline std::optional<DiagnosticsLogLevel> TryParseDiagnosticsLogLevel(std::string_view str)
  {
    for (auto level : {DiagnosticsLogLevel::None, DiagnosticsLogLevel::Error, DiagnosticsLogLevel::Warn, DiagnosticsLogLevel::Info, DiagnosticsLogLevel::Debug, DiagnosticsLogLevel::Verbose}) {
      if (StringIEquals(str, DiagnosticsLogLevelName(level))) {
        return level;
      }
    }
    return std::nullopt;
  }
}  // namespace OpenShock
