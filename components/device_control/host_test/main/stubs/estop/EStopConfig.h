#pragma once

// Host-test stub for device_control's estop/EStopConfig.h: the estop section's value and the accessors CommandHandler
// calls, backed by a plain value in host_fakes.cpp (HostFake::EStopCfg) instead of the config framework.
#include <hal/gpio_types.h>

#include <optional>

namespace OpenShock::Config {
  struct EStopConfig {
    bool enabled;
    gpio_num_t gpioPin;
  };

  [[nodiscard]] std::optional<EStopConfig> GetEStopConfig();
  [[nodiscard]] std::optional<gpio_num_t> GetEStopGpioPin();
}  // namespace OpenShock::Config
