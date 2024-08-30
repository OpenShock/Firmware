#pragma once

#include <cstdint>

namespace OpenShock::SerialInputHandler {
  [[nodiscard]] bool Init();
  void Update();

  void PrintWelcomeHeader();
  void PrintVersionInfo();
}  // namespace OpenShock::SerialInputHandler
