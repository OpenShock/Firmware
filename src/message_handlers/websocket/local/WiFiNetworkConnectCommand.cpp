#include "message_handlers/impl/WSLocal.h"

const char* const TAG = "LocalMessageHandlers";

#include "Logging.h"
#include "util/HexUtils.h"
#include "wifi/WiFiManager.h"

#include <cstdint>

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleWifiNetworkConnectCommand(uint8_t socketId, const OpenShock::Serialization::Local::LocalToHubMessage* root)
{
  (void)socketId;

  auto msg = root->payload_as_WifiNetworkConnectCommand();
  if (msg == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as WiFiNetworkConnectCommand");
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

  if (!WiFiManager::Connect(ssid->c_str())) {  // TODO: support hidden networks
    OS_LOGE(TAG, "Failed to connect to WiFi network");
  }
}
