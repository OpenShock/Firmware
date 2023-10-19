#include "MessageHandlers/Local_Private.h"

#include "GatewayConnectionManager.h"
#include "Logging.h"

#include <cstdint>

const char* const TAG = "LocalMessageHandlers";

using namespace OpenShock::MessageHandlers::Local;

void _Private::HandleGatewayPairCommand(std::uint8_t socketId, const OpenShock::Serialization::Local::LocalToDeviceMessage* root) {
  auto msg = root->payload_as_GatewayPairCommand();
  if (msg == nullptr) {
    ESP_LOGE(TAG, "Payload cannot be parsed as GatewayPairCommand");
    return;
  }

  auto code = msg->code();

  if (code == nullptr) {
    ESP_LOGE(TAG, "Gateway message is missing required properties");
    return;
  }

  if (code->size() != 6) {
    ESP_LOGE(TAG, "Gateway code is invalid (wrong length)");
    return;
  }

  GatewayConnectionManager::Pair(code->data());
}
