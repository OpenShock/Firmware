#pragma once

#include <cstdint>

namespace OpenShock::SerialInputHandler {
  [[nodiscard]] bool Init();

  bool SerialEchoEnabled();
  void SetSerialEchoEnabled(bool enabled);

  void PrintBootInfo();
  void PrintVersionInfo();
}  // namespace OpenShock::SerialInputHandler
