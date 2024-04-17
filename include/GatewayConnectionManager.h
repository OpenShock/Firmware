#pragma once

#include "AccountLinkResultCode.h"
#include "StringView.h"

#include <cstdint>
#include <functional>

namespace OpenShock::GatewayConnectionManager {
  bool Init();

  bool IsConnected();

  bool IsLinked();
  AccountLinkResultCode Link(StringView linkCode);
  void UnLink();

  bool SendMessageTXT(StringView data);
  bool SendMessageBIN(const std::uint8_t* data, std::size_t length);

  void Update();
}  // namespace OpenShock::GatewayConnectionManager
