#pragma once

#include "AccountLinkResultCode.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

namespace OpenShock::GatewayConnectionManager {
  [[nodiscard]] bool Init();

  bool IsConnected();

  bool IsLinked();
  AccountLinkResultCode Link(std::string_view linkCode);
  void UnLink();

  bool SendMessageTXT(std::string_view data);
  bool SendMessageBIN(std::span<const uint8_t> data);

  void Update();
}  // namespace OpenShock::GatewayConnectionManager
