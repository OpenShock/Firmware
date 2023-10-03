#pragma once

#include <cstdint>

namespace OpenShock::SerialInputHandler {
  void Init();
  void Update();

  void PrintWelcomeHeader();
  void PrintVersionInfo();
}  // namespace OpenShock::SerialInputHandler
