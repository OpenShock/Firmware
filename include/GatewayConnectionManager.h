#pragma once

#include "GatewayClient.h"

#include <cstdint>
#include <functional>

namespace OpenShock::GatewayConnectionManager {
  bool Init();

  bool IsConnected();

  bool IsPaired();
  bool Pair(const char* pairCode);
  void UnPair();

  void Update();
}  // namespace OpenShock::GatewayConnectionManager
