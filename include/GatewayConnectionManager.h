#pragma once

#include "AccountLinkResultCode.h"

#include <cstdint>
#include <functional>
#include <string_view>

namespace OpenShock::GatewayConnectionManager {
  [[nodiscard]] bool Init();

  bool IsConnected();

  bool IsLinked();
  AccountLinkResultCode Link(std::string_view linkCode);
  void UnLink();

  bool SendMessageTXT(std::string_view data);
  bool SendMessageBIN(const uint8_t* data, std::size_t length);

  void Update();
}  // namespace OpenShock::GatewayConnectionManager
