#pragma once

#include "SetGPIOResultCode.h"
#include "ShockerCommandType.h"
#include "ShockerModelType.h"

#include <cstdint>

// TODO: This is horrible architecture. Fix it.

namespace OpenShock::CommandHandler {
  [[nodiscard]] bool Init();
  bool Ok();

  SetGPIOResultCode SetRfTxPin(uint8_t txPin);
  uint8_t GetRfTxPin();

  SetGPIOResultCode SetEstopPin(uint8_t estopPin);
  uint8_t GetEstopPin();

  bool SetKeepAliveEnabled(bool enabled);
  bool SetKeepAlivePaused(bool paused);

  bool HandleCommand(ShockerModelType shockerModel, uint16_t shockerId, ShockerCommandType type, uint8_t intensity, uint16_t durationMs);
}  // namespace OpenShock::CommandHandler
