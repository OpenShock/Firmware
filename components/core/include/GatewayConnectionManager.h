#pragma once

#include "enums/AccountLinkResultCode.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace OpenShock::GatewayConnectionManager {
  [[nodiscard]] bool Init();

  bool IsConnected();

  bool IsLinked();
  AccountLinkResultCode Link(std::string_view linkCode);
  void UnLink();
  /// @brief Stores a backend auth token directly (e.g. from the serial console) and reconnects with it.
  bool SetAuthToken(std::string authToken);

  bool SendMessageTXT(std::string_view data);
  bool SendMessageBIN(std::span<const uint8_t> data);

  void MarkPingReceived();

  void Update();
}  // namespace OpenShock::GatewayConnectionManager
