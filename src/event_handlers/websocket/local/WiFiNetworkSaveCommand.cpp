#include "event_handlers/impl/WSLocal.h"

const char* const TAG = "LocalMessageHandlers";

#include "Logging.h"
#include "util/HexUtils.h"
#include "wifi/WiFiManager.h"

#include <cstdint>

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleWiFiNetworkSaveCommand(uint8_t socketId, const OpenShock::Serialization::Local::LocalToHubMessage* root) {
  (void)socketId;
  
  auto msg = root->payload_as_WifiNetworkSaveCommand();
  if (msg == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as WiFiNetworkSaveCommand");
    return;
  }

  auto ssid     = msg->ssid();
  auto password = msg->password() ? msg->password()->str() : "";

  if (ssid == nullptr) {
    OS_LOGE(TAG, "WiFi message is missing SSID");
    return;
  }

  if (ssid->size() > 31) {
    OS_LOGE(TAG, "WiFi SSID is too long");
    return;
  }

  std::size_t passwordLength = password.size();

  if (passwordLength != 0 && passwordLength < 8) {
    OS_LOGE(TAG, "WiFi password is too short");
    return;
  }

  if (passwordLength > 63) {
    OS_LOGE(TAG, "WiFi password is too long");
    return;
  }

  if (!WiFiManager::Save(ssid->c_str(), password)) {
    OS_LOGE(TAG, "Failed to save WiFi network");
  }
}
