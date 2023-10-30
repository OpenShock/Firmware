#pragma once

#include "GatewayClient.h"
#include "GatewayPairResultCode.h"

#include <cstdint>
#include <functional>

namespace OpenShock::GatewayConnectionManager {
  bool Init();

  bool IsConnected();

  bool IsPaired();
  GatewayPairResultCode Pair(const char* pairCode);
  void UnPair();

  void Update();
}  // namespace OpenShock::GatewayConnectionManager
