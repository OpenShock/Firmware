#pragma once

#include <cstdint>
#include <functional>

namespace OpenShock::GatewayConnectionManager {
  bool Init();

  bool IsConnected();

  bool IsPaired();
  bool Pair(const char* pairCode);
  void UnPair();

  typedef std::function<void(bool)> ConnectedChangedHandler;
  std::uint64_t RegisterConnectedChangedHandler(ConnectedChangedHandler handler);
  void UnRegisterConnectedChangedHandler(std::uint64_t handlerId);

  void Update();
}  // namespace OpenShock::GatewayConnectionManager
