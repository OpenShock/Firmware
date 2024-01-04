#pragma once

#include "serialization/CallbackFn.h"
#include "wifi/WiFiScanStatus.h"

#include "serialization/_fbs/DeviceToLocalMessage_generated.h"
#include "serialization/_fbs/WifiNetworkEventType_generated.h"

#include <esp_wifi_types.h>

namespace OpenShock {
  class WiFiNetwork;
}

namespace OpenShock::Serialization::Local {
  bool SerializeErrorMessage(const char* message, Common::SerializationCallbackFn callback);
  bool SerializeReadyMessage(const WiFiNetwork* connectedNetwork, bool accountLinked, Common::SerializationCallbackFn callback);
  bool SerializeWiFiScanStatusChangedEvent(OpenShock::WiFiScanStatus status, Common::SerializationCallbackFn callback);
  bool SerializeWiFiNetworkEvent(Types::WifiNetworkEventType eventType, const WiFiNetwork& network, Common::SerializationCallbackFn callback);
  bool SerializeWiFiIpAddressChangedEvent(const char* ipAddress, Common::SerializationCallbackFn callback);
  bool SerializeAccountLinkCommandResult(AccountLinkResultCode resultCode, Common::SerializationCallbackFn callback);
  bool SerializeSetRfTxPinCommandResult(std::uint8_t pin, SetRfPinResultCode resultCode, Common::SerializationCallbackFn callback);
}  // namespace OpenShock::Serialization::Local
