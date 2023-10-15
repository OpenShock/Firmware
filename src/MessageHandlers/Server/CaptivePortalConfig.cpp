#include "MessageHandlers/Server_Private.h"

#include "CaptivePortal.h"

#include <esp_log.h>

#include <cstdint>

const char* const TAG = "ServerMessageHandlers";

using namespace OpenShock::MessageHandlers::Server;

void _Private::HandleCaptivePortalConfig(std::uint8_t socketId, const OpenShock::Serialization::ServerToDeviceMessage* root) {
  auto msg = root->payload_as_CaptivePortalConfig();
  if (msg == nullptr) {
    ESP_LOGE(TAG, "Payload cannot be parsed as CaptivePortalConfig");
    return;
  }

  bool enabled = msg->enabled();

  ESP_LOGD(TAG, "Captive portal is %s", enabled ? "force enabled" : "normal");

  OpenShock::CaptivePortal::SetAlwaysEnabled(enabled);
}
