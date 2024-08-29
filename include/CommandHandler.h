#pragma once

#include "SetRfPinResultCode.h"
#include "ShockerCommandType.h"
#include "ShockerModelType.h"

#include <cstdint>

// TODO: This is horrible architecture. Fix it.

namespace OpenShock::CommandHandler {
  bool Init();
  bool Ok();

  SetRfPinResultCode SetRfTxPin(uint8_t txPin);
  uint8_t GetRfTxPin();

  bool SetKeepAliveEnabled(bool enabled);
  bool SetKeepAlivePaused(bool paused);

  bool HandleCommand(ShockerModelType shockerModel, uint16_t shockerId, ShockerCommandType type, uint8_t intensity, uint16_t durationMs);
}  // namespace OpenShock::CommandHandler
