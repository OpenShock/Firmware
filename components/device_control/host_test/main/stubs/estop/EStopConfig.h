#pragma once

// Host-test stub for device_control's estop/EStopConfig.h: the estop section's value and the accessors CommandHandler
// calls, backed by a plain value in host_fakes.cpp (HostFake::EStopCfg) instead of the config framework.
#include <hal/gpio_types.h>

namespace OpenShock::Config {
  struct EStopConfig {
    bool enabled;
    gpio_num_t gpioPin;
  };

  [[nodiscard]] EStopConfig GetEStopConfig();
  [[nodiscard]] gpio_num_t GetEStopGpioPin();
}  // namespace OpenShock::Config
