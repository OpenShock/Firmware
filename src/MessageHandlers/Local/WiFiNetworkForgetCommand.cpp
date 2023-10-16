#include "MessageHandlers/Local_Private.h"

#include "Utils/HexUtils.h"
#include "WiFiManager.h"

#include <esp_log.h>

#include <nonstd/span.hpp>

#include <cstdint>

const char* const TAG = "LocalMessageHandlers";

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleWiFiNetworkForgetCommand(std::uint8_t socketId, const OpenShock::Serialization::Local::LocalToDeviceMessage* root) {
  auto msg = root->payload_as_WifiNetworkForgetCommand();
  if (msg == nullptr) {
    ESP_LOGE(TAG, "Payload cannot be parsed as WiFiNetworkForgetCommand");
    return;
  }

  auto ssid = msg->ssid();

  if (ssid == nullptr) {
    ESP_LOGE(TAG, "WiFi message is missing required properties");
    return;
  }

  if (ssid->size() > 31) {
    ESP_LOGE(TAG, "WiFi SSID is too long");
    return;
  }

  if (!WiFiManager::Forget(ssid->c_str())) {  // TODO: support hidden networks
    ESP_LOGE(TAG, "Failed to forget WiFi network");
  }
}
