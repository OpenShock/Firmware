#include "event_handlers/impl/WSLocal.h"

#include "Logging.h"
#include "wifi/WiFiScanManager.h"

const char* const TAG = "LocalMessageHandlers";

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleWiFiScanCommand(std::uint8_t socketId, const OpenShock::Serialization::Local::LocalToHubMessage* root) {
  (void)socketId;
  
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
