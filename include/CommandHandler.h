#pragma once

#include "SetGPIOResultCode.h"
#include "ShockerCommandType.h"
#include "ShockerModelType.h"

#include <hal/gpio_types.h>

#include <cstdint>

// TODO: This is horrible architecture. Fix it.

namespace OpenShock::CommandHandler {
  [[nodiscard]] bool Init();
  bool Ok();

  gpio_num_t GetRfTxPin();
  SetGPIOResultCode SetRfTxPin(gpio_num_t txPin);

  SetGPIOResultCode SetEStopPin(gpio_num_t estopPin);
  gpio_num_t GetEstopPin();

  bool SetKeepAliveEnabled(bool enabled);
  bool SetKeepAlivePaused(bool paused);

  bool HandleCommand(ShockerModelType shockerModel, uint16_t shockerId, ShockerCommandType type, uint8_t intensity, uint16_t durationMs);
}  // namespace OpenShock::CommandHandler
