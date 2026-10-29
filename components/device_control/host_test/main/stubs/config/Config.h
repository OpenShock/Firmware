#pragma once

// Host-test stub for config's Config.h: just the RF/EStop accessors device_control
// calls, backed by plain values in host_fakes.cpp (HostFake::RfConfig/EStopCfg).
#include <hal/gpio_types.h>

#include <optional>

namespace OpenShock::Config {
  struct RFConfig {
    gpio_num_t txPin;
    bool keepAliveEnabled;
  };

  struct EStopConfig {
    bool enabled;
    gpio_num_t gpioPin;
  };

  [[nodiscard]] std::optional<RFConfig> GetRFConfig();
  [[nodiscard]] std::optional<gpio_num_t> GetRFConfigTxPin();
  bool SetRFConfigTxPin(gpio_num_t txPin);
  [[nodiscard]] std::optional<bool> GetRFConfigKeepAliveEnabled();
  bool SetRFConfigKeepAliveEnabled(bool enabled);

  [[nodiscard]] std::optional<EStopConfig> GetEStopConfig();
  [[nodiscard]] std::optional<gpio_num_t> GetEStopGpioPin();
}  // namespace OpenShock::Config
