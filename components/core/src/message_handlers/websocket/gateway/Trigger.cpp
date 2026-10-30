#include "message_handlers/impl/WSGateway.h"

const char* const TAG = "ServerMessageHandlers";

#include "AppHooks.h"
#include "estop/EStopManager.h"
#include "GatewayConnectionManager.h"
#include "Logging.h"

#include <cstdint>
#include <utility>

using namespace OpenShock::MessageHandlers::Server;

using TriggerType = OpenShock::Serialization::Gateway::TriggerType;

void _Private::HandleTrigger(OpenShock::GatewayClient&, const OpenShock::Serialization::Gateway::GatewayToHubMessage* root)
{
  auto msg = root->payload_as_Trigger();
  if (msg == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as Trigger");
    return;
  }

  if (EStopManager::IsEStopped()) {
    OS_LOGD(TAG, "Ignoring trigger command due to EmergencyStop being activated");
    return;
  }

  auto triggerType = msg->type();

  // No default: -Wswitch flags new trigger types. The value comes from the network, so fall through to the log below.
  switch (triggerType) {
    case TriggerType::Restart:
      // Handled on the websocket task: the actual restart runs from the main task so the client can close cleanly.
      GatewayConnectionManager::RequestRestart();
      return;
    case TriggerType::EmergencyStop:
      EStopManager::SoftwareTrigger();
      return;
    case TriggerType::CaptivePortalEnable:
      OpenShock::AppHooks::CaptivePortalSetAlwaysEnabled(true);
      return;
    case TriggerType::CaptivePortalDisable:
      OpenShock::AppHooks::CaptivePortalSetAlwaysEnabled(false);
      return;
  }

  OS_LOGW(TAG, "Got unknown trigger type: %hhu", std::to_underlying(triggerType));
}
