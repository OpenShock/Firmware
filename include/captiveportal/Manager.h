#pragma once

#include <cstdint>
#include <string_view>

#include "span.h"

namespace OpenShock::CaptivePortal {
  [[nodiscard]] bool Init();

  void SetAlwaysEnabled(bool alwaysEnabled);
  bool IsAlwaysEnabled();

  /// @brief Signal that the user has completed setup. The portal will close once the device is fully online.
  void SetUserDone();

  bool ForceClose(uint32_t timeoutMs);

  bool IsRunning();

  bool SendMessageTXT(uint8_t socketId, std::string_view data);
  bool SendMessageBIN(uint8_t socketId, tcb::span<const uint8_t> data);

  bool BroadcastMessageTXT(std::string_view data);
  bool BroadcastMessageBIN(tcb::span<const uint8_t> data);
}  // namespace OpenShock::CaptivePortal
