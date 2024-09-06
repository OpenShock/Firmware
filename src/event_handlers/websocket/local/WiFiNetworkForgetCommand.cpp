#include "event_handlers/impl/WSLocal.h"

const char* const TAG = "LocalMessageHandlers";

#include "Logging.h"
#include "util/HexUtils.h"
#include "wifi/WiFiManager.h"

#include <cstdint>

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleWiFiNetworkForgetCommand(uint8_t socketId, const OpenShock::Serialization::Local::LocalToHubMessage* root) {
  (void)socketId;
  
  auto msg = root->payload_as_WifiNetworkForgetCommand();
  if (msg == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as WiFiNetworkForgetCommand");
    return;
  }

  auto ssid = msg->ssid();

  if (ssid == nullptr) {
    OS_LOGE(TAG, "WiFi message is missing required properties");
    return;
  }

  if (ssid->size() > 31) {
    OS_LOGE(TAG, "WiFi SSID is too long");
    return;
  }

  if (!WiFiManager::Forget(ssid->c_str())) {  // TODO: support hidden networks
    OS_LOGE(TAG, "Failed to forget WiFi network");
  }
}
