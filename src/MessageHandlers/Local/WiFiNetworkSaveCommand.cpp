#include "MessageHandlers/Local_Private.h"

#include "Utils/HexUtils.h"
#include "WiFiManager.h"

#include <esp_log.h>

#include <nonstd/span.hpp>

#include <cstdint>

const char* const TAG = "LocalMessageHandlers";

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleWiFiNetworkSaveCommand(std::uint8_t socketId, const OpenShock::Serialization::Local::LocalToDeviceMessage* root) {
  auto msg = root->payload_as_WifiNetworkSaveCommand();
  if (msg == nullptr) {
    ESP_LOGE(TAG, "Payload cannot be parsed as WiFiNetworkSaveCommand");
    return;
  }

  auto ssid     = msg->ssid();
  auto bssid    = msg->bssid();
  auto password = msg->password();

  if (ssid == nullptr || bssid == nullptr || password == nullptr) {
    ESP_LOGE(TAG, "WiFi message is missing required properties");
    return;
  }

  if (ssid->size() > 31) {
    ESP_LOGE(TAG, "WiFi SSID is too long");
    return;
  }

  if (bssid->size() != 17) {
    ESP_LOGE(TAG, "WiFi BSSID is invalid (wrong length)");
    return;
  }

  // Convert BSSID to byte array
  std::uint8_t bssidBytes[6];
  if (!HexUtils::TryParseHexMac<17>(nonstd::span<const char, 17>(bssid->data(), bssid->size()), bssidBytes)) {
    ESP_LOGE(TAG, "WiFi BSSID is invalid (failed to parse)");
    return;
  }

  if (password->size() > 63) {
    ESP_LOGE(TAG, "WiFi password is too long");
    return;
  }

  if (!WiFiManager::Save(bssidBytes, password->str())) {  // TODO: support SSID as well
    ESP_LOGE(TAG, "Failed to save WiFi network");
  }
}
