#include "MessageHandlers/Local_Private.h"

#include "Utils/HexUtils.h"
#include "WiFiManager.h"

#include <esp_log.h>

#include <nonstd/span.hpp>

#include <cstdint>

const char* const TAG = "LocalMessageHandlers";

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleWiFiNetworkConnectCommand(std::uint8_t socketId, const OpenShock::Serialization::Local::LocalToDeviceMessage* root) {
  auto msg = root->payload_as_WifiNetworkConnectCommand();
  if (msg == nullptr) {
    ESP_LOGE(TAG, "Payload cannot be parsed as WiFiNetworkConnectCommand");
    return;
  }

  auto ssid  = msg->ssid();
  auto bssid = msg->bssid();

  if (ssid == nullptr && bssid == nullptr) {
    ESP_LOGE(TAG, "WiFi message is missing required properties");
    return;
  }

  if (ssid != nullptr && ssid->size() > 31) {
    ESP_LOGE(TAG, "WiFi SSID is too long");
    return;
  }

  if (bssid != nullptr && bssid->size() != 17) {
    ESP_LOGE(TAG, "WiFi BSSID is invalid (wrong length)");
    return;
  }

  // Convert BSSID to byte array
  std::uint8_t bssidBytes[6];
  if (bssid != nullptr && !HexUtils::TryParseHexMac<17>(nonstd::span<const char, 17>(bssid->data(), bssid->size()), bssidBytes)) {
    ESP_LOGE(TAG, "WiFi BSSID is invalid (failed to parse)");
    return;
  }

  if (!WiFiManager::Connect(bssidBytes)) {  // TODO: support SSID as well
    ESP_LOGE(TAG, "Failed to connect to WiFi network");
  }
}
