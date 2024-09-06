#include "event_handlers/impl/WSGateway.h"

const char* const TAG = "ServerMessageHandlers";

#include "CaptivePortal.h"
#include "Logging.h"

#include <cstdint>

using namespace OpenShock::MessageHandlers::Server;

void _Private::HandleCaptivePortalConfig(const OpenShock::Serialization::Gateway::GatewayToHubMessage* root) {
  auto msg = root->payload_as_CaptivePortalConfig();
  if (msg == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as CaptivePortalConfig");
    return;
  }

  bool enabled = msg->enabled();

  OS_LOGD(TAG, "Captive portal is %s", enabled ? "force enabled" : "normal");

  OpenShock::CaptivePortal::SetAlwaysEnabled(enabled);
}
