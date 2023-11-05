#include "event_handlers/impl/WSLocal.h"

#include "Logging.h"
#include "Utils/HexUtils.h"
#include "wifi/WiFiManager.h"

#include <nonstd/span.hpp>

#include <cstdint>

const char* const TAG = "LocalMessageHandlers";

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleWiFiNetworkDisconnectCommand(std::uint8_t socketId, const OpenShock::Serialization::Local::LocalToDeviceMessage* root) {
  auto msg = root->payload_as_WifiNetworkDisconnectCommand();
  if (msg == nullptr) {
    ESP_LOGE(TAG, "Payload cannot be parsed as WiFiNetworkDisconnectCommand");
    return;
  }

  WiFiManager::Disconnect();
}
