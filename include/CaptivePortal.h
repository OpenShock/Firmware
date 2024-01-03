#pragma once

#include "StringView.h"

#include <cstdint>

namespace OpenShock::CaptivePortal {
  void SetAlwaysEnabled(bool alwaysEnabled);
  bool IsAlwaysEnabled();

  bool IsRunning();
  void Update();

  bool SendMessageTXT(std::uint8_t socketId, StringView data);
  bool SendMessageBIN(std::uint8_t socketId, const std::uint8_t* data, std::size_t len);

  bool BroadcastMessageTXT(StringView data);
  bool BroadcastMessageBIN(const std::uint8_t* data, std::size_t len);
}  // namespace OpenShock::CaptivePortal
