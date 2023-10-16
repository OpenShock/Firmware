#include "MessageHandlers/Local_Private.h"

#include "GatewayConnectionManager.h"

#include <esp_log.h>

#include <cstdint>

const char* const TAG = "LocalMessageHandlers";

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleGatewayUnpairCommand(std::uint8_t socketId, const OpenShock::Serialization::Local::LocalToDeviceMessage* root) {
  auto msg = root->payload_as_GatewayUnpairCommand();
  if (msg == nullptr) {
    ESP_LOGE(TAG, "Payload cannot be parsed as GatewayUnpairCommand");
    return;
  }

  GatewayConnectionManager::UnPair();
}
