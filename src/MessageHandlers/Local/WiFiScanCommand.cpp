#include "MessageHandlers/Local_Private.h"

#include "WiFiScanManager.h"

#include <esp_log.h>

const char* const TAG = "LocalMessageHandlers";

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleWiFiScanCommand(std::uint8_t socketId, const OpenShock::Serialization::Local::LocalToDeviceMessage* root) {
  auto msg = root->payload_as_WifiScanCommand();
  if (msg == nullptr) {
    ESP_LOGE(TAG, "Payload cannot be parsed as WiFiScanCommand");
    return;
  }

  if (msg->run()) {
    WiFiScanManager::StartScan();
  } else {
    WiFiScanManager::AbortScan();
  }
}
