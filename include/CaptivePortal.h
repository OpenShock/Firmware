#pragma once

#include <cstdint>
#include <string_view>

namespace OpenShock::CaptivePortal {
  void SetAlwaysEnabled(bool alwaysEnabled);
  bool IsAlwaysEnabled();

  bool ForceClose(uint32_t timeoutMs);

  bool IsRunning();
  void Update();

  bool SendMessageTXT(uint8_t socketId, std::string_view data);
  bool SendMessageBIN(uint8_t socketId, const uint8_t* data, std::size_t len);

  bool BroadcastMessageTXT(std::string_view data);
  bool BroadcastMessageBIN(const uint8_t* data, std::size_t len);
}  // namespace OpenShock::CaptivePortal
