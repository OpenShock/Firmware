#pragma once

#include "estop/EStopState.h"

#include <hal/gpio_types.h>

#include <cstdint>

namespace OpenShock::EStopManager {
  /// @brief Restores an E-Stop latched before the restart (the `latched` key of the estop section, estop/EStopConfig.h)
  ///        and starts sampling the input. Call before anything can transmit. While the task runs, every activation and
  ///        every clear is written to that key, so a reboot or power cut never releases an active E-Stop.
  [[nodiscard]] bool Init();
  // Both apply the change to the running E-Stop and then persist it to the config (like CommandHandler::SetRfTxPin),
  // so callers don't have to keep the two in sync. They refuse while the E-Stop is active.
  bool SetEStopEnabled(bool enabled);
  bool SetEStopPin(gpio_num_t pin);
  bool IsEStopped();
  int64_t LastEStopped();

  void SoftwareTrigger();
}  // namespace OpenShock::EStopManager
