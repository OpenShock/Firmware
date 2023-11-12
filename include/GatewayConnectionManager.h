#pragma once

#include "AccountLinkResultCode.h"

#include <cstdint>
#include <functional>

namespace OpenShock::GatewayConnectionManager {
  bool Init();

  bool IsConnected();

  bool IsPaired();
  AccountLinkResultCode Pair(const char* pairCode);
  void UnPair();

  void Update();
}  // namespace OpenShock::GatewayConnectionManager
