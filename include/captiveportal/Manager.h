#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace OpenShock::CaptivePortal {
  [[nodiscard]] bool Init();

  void SetAlwaysEnabled(bool alwaysEnabled);
  bool IsAlwaysEnabled();

  /// @brief Signal that the user has completed setup. The portal will close once the device is fully online.
  void SetUserDone();

  bool ForceClose(uint32_t timeoutMs);

  bool IsRunning();

  /// @brief The fixed IPv4 address the captive portal AP serves from (e.g. "4.3.2.1").
  const char* ApIPv4String();

  bool SendMessageTXT(uint8_t socketId, std::string_view data);
  bool SendMessageBIN(uint8_t socketId, std::span<const uint8_t> data);

  bool BroadcastMessageTXT(std::string_view data);
  bool BroadcastMessageBIN(std::span<const uint8_t> data);
}  // namespace OpenShock::CaptivePortal
