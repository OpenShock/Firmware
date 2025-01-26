#pragma once

namespace OpenShock::Serial {
  enum class SerialReadResult {
    NoData,
    Data,
    LineEnd,
    AutoCompleteRequest,
  };
}  // namespace OpenShock::Serial
