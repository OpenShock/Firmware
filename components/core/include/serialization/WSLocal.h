#pragma once

#include "serialization/CallbackFn.h"
#include "wifi/WiFiScanStatus.h"

#include "serialization/_fbs/WifiNetworkEventType_generated.h"

#include <cstdint>
#include <string_view>

namespace OpenShock {
  class WiFiNetwork;
}

namespace OpenShock::VisualStateManager {
  struct LedStatusNotice;
}

namespace OpenShock::Serialization::Local {
  bool SerializeErrorMessage(std::string_view message, Common::SerializationCallbackFn callback);
  /// @param clientId The socket id of the client the message goes to, which LED events name as an override's owner
  bool SerializeReadyMessage(const WiFiNetwork* connectedNetwork, bool accountLinked, uint8_t clientId, Common::SerializationCallbackFn callback);
  bool SerializeWiFiScanStatusChangedEvent(OpenShock::WiFiScanStatus status, Common::SerializationCallbackFn callback);
  bool SerializeWiFiNetworkEvent(Types::WifiNetworkEventType eventType, const WiFiNetwork& network, Common::SerializationCallbackFn callback);
  bool SerializeWiFiNetworksEvent(Types::WifiNetworkEventType eventType, const std::vector<WiFiNetwork>& networks, Common::SerializationCallbackFn callback);
  bool SerializeWiFiGotIpEvent(const char* ip, Common::SerializationCallbackFn callback);
  bool SerializeAccountLinkStatusEvent(bool linked, Common::SerializationCallbackFn callback);
  bool SerializeLedSettingsChangedEvent(Common::SerializationCallbackFn callback);
  /// @brief The current LED status, with what `notice` reports beyond it.
  bool SerializeLedStatusEvent(const VisualStateManager::LedStatusNotice& notice, Common::SerializationCallbackFn callback);
}  // namespace OpenShock::Serialization::Local
