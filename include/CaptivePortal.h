#pragma once

#include "StringView.h"

#include <freertos/portmacro.h>

#include <cstdint>

namespace OpenShock::CaptivePortal {
  void SetAlwaysEnabled(bool alwaysEnabled);
  bool IsAlwaysEnabled();

  bool ForceClose(uint32_t timeoutMs);

  bool IsRunning();
  void Update();

  bool SendMessageTXT(uint8_t socketId, StringView data);
  bool SendMessageBIN(uint8_t socketId, const uint8_t* data, std::size_t len);

  bool BroadcastMessageTXT(StringView data);
  bool BroadcastMessageBIN(const uint8_t* data, std::size_t len);
}  // namespace OpenShock::CaptivePortal
