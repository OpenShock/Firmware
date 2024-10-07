#pragma once

#include <cstdint>

namespace OpenShock::SerialInputHandler {
  [[nodiscard]] bool Init();
  void Update();

  bool SerialEchoEnabled();
  void SetSerialEchoEnabled(bool enabled);

  void PrintWelcomeHeader();
  void PrintVersionInfo();
}  // namespace OpenShock::SerialInputHandler
