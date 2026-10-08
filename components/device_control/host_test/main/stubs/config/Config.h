#pragma once

// Host-test stub for config's Config.h: just the RF/EStop accessors device_control
// calls, backed by plain values in host_fakes.cpp (HostFake::RfConfig/EStopCfg).
#include <hal/gpio_types.h>

namespace OpenShock::Config {
  struct RFConfig {
    gpio_num_t txPin;
    bool keepAliveEnabled;
  };

  struct EStopConfig {
    bool enabled;
    gpio_num_t gpioPin;
  };

  bool GetRFConfig(RFConfig& out);
  bool GetRFConfigTxPin(gpio_num_t& out);
  bool SetRFConfigTxPin(gpio_num_t txPin);
  bool GetRFConfigKeepAliveEnabled(bool& out);
  bool SetRFConfigKeepAliveEnabled(bool enabled);

  bool GetEStopConfig(EStopConfig& out);
  bool GetEStopGpioPin(gpio_num_t& out);
}  // namespace OpenShock::Config
