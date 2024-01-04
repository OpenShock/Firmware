#include "event_handlers/impl/WSGateway.h"

#include "CaptivePortal.h"
#include "Logging.h"

#include <cstdint>

const char* const TAG = "ServerMessageHandlers";

using namespace OpenShock::MessageHandlers::Server;

void _Private::HandleCaptivePortalConfig(const OpenShock::Serialization::Gateway::GatewayToDeviceMessage* root) {
  auto msg = root->payload_as_CaptivePortalConfig();
  if (msg == nullptr) {
    ESP_LOGE(TAG, "Payload cannot be parsed as CaptivePortalConfig");
    return;
  }

  bool enabled = msg->enabled();

  ESP_LOGD(TAG, "Captive portal is %s", enabled ? "force enabled" : "normal");

  OpenShock::CaptivePortal::SetAlwaysEnabled(enabled);
}
