#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace OpenShock::CaptivePortal {
  [[nodiscard]] bool Init();

  void SetAlwaysEnabled(bool alwaysEnabled);
  bool IsAlwaysEnabled();

  bool ForceClose(uint32_t timeoutMs);

  bool IsRunning();

  bool SendMessageTXT(uint8_t socketId, std::string_view data);
  bool SendMessageBIN(uint8_t socketId, std::span<const uint8_t> data);

  bool BroadcastMessageTXT(std::string_view data);
  bool BroadcastMessageBIN(std::span<const uint8_t> data);
}  // namespace OpenShock::CaptivePortal
