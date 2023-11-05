#pragma once

#include "serialization/CallbackFn.h"
#include "wifi/WiFiScanStatus.h"

#include <esp_wifi_types.h>

namespace OpenShock {
  class WiFiNetwork;
}

namespace OpenShock::Serialization::Local {
  bool SerializeErrorMessage(const char* message, Common::SerializationCallbackFn callback);
  bool SerializeReadyMessage(const WiFiNetwork* connectedNetwork, bool gatewayPaired, std::uint8_t radioTxPin, Common::SerializationCallbackFn callback);
  bool SerializeWiFiScanStatusChangedEvent(OpenShock::WiFiScanStatus status, Common::SerializationCallbackFn callback);
  bool SerializeWiFiScanNetworkDiscoveredEvent(const wifi_ap_record_t* ap_record, bool isSaved, Common::SerializationCallbackFn callback);
  bool SerializeWiFiNetworkSavedEvent(const WiFiNetwork& network, Common::SerializationCallbackFn callback);
  bool SerializeWiFiNetworkLostEvent(const WiFiNetwork& network, Common::SerializationCallbackFn callback);
  bool SerializeWiFiNetworkUpdatedEvent(const WiFiNetwork& network, Common::SerializationCallbackFn callback);
  bool SerializeWiFiNetworkConnectedEvent(const WiFiNetwork& network, Common::SerializationCallbackFn callback);
  bool SerializeWiFiNetworkDiscoveredEvent(const WiFiNetwork& network, Common::SerializationCallbackFn callback);
}  // namespace OpenShock::Serialization::Local
