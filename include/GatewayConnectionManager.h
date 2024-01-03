#pragma once

#include "AccountLinkResultCode.h"

#include <cstdint>
#include <functional>

namespace OpenShock::GatewayConnectionManager {
  bool Init();

  bool IsConnected();

  bool IsLinked();
  AccountLinkResultCode Link(const char* linkCode);
  void UnLink();

  void Update();
}  // namespace OpenShock::GatewayConnectionManager
