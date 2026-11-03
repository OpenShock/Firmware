#pragma once

// Host-test stub for device_control's radio/RFConfig.h: the rf section's value and the accessors CommandHandler and
// RFTransmitter call, backed by a plain value in host_fakes.cpp (HostFake::RfConfig) instead of the config framework.
#include <hal/gpio_types.h>

namespace OpenShock::Config {
  struct RFConfig {
    gpio_num_t txPin;
    bool keepAliveEnabled;
  };

  [[nodiscard]] RFConfig GetRFConfig();
  [[nodiscard]] gpio_num_t GetRFConfigTxPin();
  bool SetRFConfigTxPin(gpio_num_t txPin);
  [[nodiscard]] bool GetRFConfigKeepAliveEnabled();
  bool SetRFConfigKeepAliveEnabled(bool enabled);
}  // namespace OpenShock::Config
