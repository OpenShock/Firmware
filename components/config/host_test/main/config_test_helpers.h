#pragma once

// Shared helpers for the config host tests. The suite runs the config framework (components/config) together with
// every module's section (device_control's rf and estop, core's wifi, backend and otaUpdate, captive_portal's
// captivePortal, serial_console's serialInput), so it checks the store as the firmware assembles it: over ESP-IDF's
// real NVS on the linux flash emulation.
#include "config/Nvs.h"
#include "config/Store.h"

#include "BackendConfig.h"
#include "captiveportal/CaptivePortalConfig.h"
#include "estop/EStopConfig.h"
#include "OtaUpdateConfig.h"
#include "radio/RFConfig.h"
#include "serial_console/SerialInputConfig.h"
#include "visual/LedConfig.h"
#include "wifi/WiFiConfig.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ConfigTest {
  // The storage format the tests check against: the store's own names and each module's. Written out here on purpose,
  // so a renamed key or namespace (which would orphan what devices hold) fails a test.
  namespace Keys {
    inline constexpr const char* kPartitionCfg = "cfg";
    namespace Ns {
      inline constexpr const char* kBackend = "backend";
      inline constexpr const char* kWifi    = "wifi";
      inline constexpr const char* kWifiCfg = "wifi.cfg";
      inline constexpr const char* kRf      = "rf";
      inline constexpr const char* kEstop   = "estop";
      inline constexpr const char* kPortal  = "portal";
      inline constexpr const char* kSerial  = "serial";
      inline constexpr const char* kOta     = "ota";
      inline constexpr const char* kLed     = "led";
      inline constexpr const char* kAll[]   = {kBackend, kWifi, kWifiCfg, kRf, kEstop, kPortal, kSerial, kOta, kLed};
    }  // namespace Ns
    namespace Backend {
      inline constexpr const char* kDomain = "domain";
      inline constexpr const char* kToken  = "token";
    }  // namespace Backend
    namespace WifiCfg {
      inline constexpr const char* kApSsid   = "ap_ssid";
      inline constexpr const char* kHostname = "hostname";
    }  // namespace WifiCfg
    namespace Rf {
      inline constexpr const char* kTxPin     = "tx_pin";
      inline constexpr const char* kKeepAlive = "keepalive";
    }  // namespace Rf
    namespace Estop {
      inline constexpr const char* kEnabled = "enabled";
      inline constexpr const char* kGpio    = "gpio";
    }  // namespace Estop
    namespace Ota {
      inline constexpr const char* kUpdateId = "update_id";
    }
  }  // namespace Keys

  // Every section's value at once: what a hub's whole config is. Default-constructed, every member holds its defaults.
  struct Full {
    OpenShock::Config::RFConfig rf;
    OpenShock::Config::WiFiConfig wifi;
    OpenShock::Config::CaptivePortalConfig captivePortal;
    OpenShock::Config::BackendConfig backend;
    OpenShock::Config::SerialInputConfig serialInput;
    OpenShock::Config::OtaUpdateConfig otaUpdate;
    OpenShock::Config::EStopConfig estop;
    OpenShock::Config::LedConfig led;

    // Every section's keys
    bool SaveNvs(const char* partition) const;
  };

  // Every field differs from the defaults.
  Full MakeSampleConfig();

  // Erases the `cfg` partition: a hub with nothing stored.
  void WipeStore();

  // WipeStore + Config::Init: a hub booting on the defaults.
  void InitDefault();

  // A reboot: Config::Init again on whatever the store holds.
  void Reload();

  // Writes `config` to the store as the firmware leaves it, then Reload()s: a hub running it.
  void InitWith(const Full& config);

  // The number of entries the `cfg` partition has in use: unchanged across a save that changes nothing.
  std::size_t UsedEntries();

  // Whether `ns` exists in `partition`: it was created by a write once, and stays even when every key of it has since
  // been erased.
  bool NamespaceExists(const char* partition, const char* ns);

  // Reads back every section through the modules' public accessors.
  Full Snapshot();

  // TEST_ASSERTs that every field of every section matches. When withSensitiveData
  // is false, passwords and the backend auth token are expected to be empty.
  void AssertConfigEqual(const Full& expected, const Full& actual, bool withSensitiveData = true);
  void AssertCredentialsEqual(const OpenShock::Config::WiFiCredentials& expected, const OpenShock::Config::WiFiCredentials& actual, bool withSensitiveData = true);
}  // namespace ConfigTest
