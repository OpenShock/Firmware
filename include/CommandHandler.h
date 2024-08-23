#pragma once

#include "SetGPIOResultCode.h"
#include "ShockerCommandType.h"
#include "ShockerModelType.h"

#include <cstdint>

// TODO: This is horrible architecture. Fix it.

namespace OpenShock::CommandHandler {
  bool Init();
  bool Ok();

  SetGPIOResultCode SetRfTxPin(std::uint8_t txPin);
  std::uint8_t GetRfTxPin();

  SetGPIOResultCode SetEstopPin(std::uint8_t estopPin);
  std::uint8_t GetEstopPin();

  bool SetKeepAliveEnabled(bool enabled);
  bool SetKeepAlivePaused(bool paused);

  bool HandleCommand(ShockerModelType shockerModel, std::uint16_t shockerId, ShockerCommandType type, std::uint8_t intensity, std::uint16_t durationMs);
}  // namespace OpenShock::CommandHandler
