#pragma once

#include <cstdint>

namespace OpenShock::SerialInputHandler {
  bool Init();
  void Update();

  void PrintWelcomeHeader();
  void PrintVersionInfo();
}  // namespace OpenShock::SerialInputHandler
