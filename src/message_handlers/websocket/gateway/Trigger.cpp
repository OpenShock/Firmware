#include "message_handlers/impl/WSGateway.h"

const char* const TAG = "ServerMessageHandlers";

#include "captiveportal/Manager.h"
#include "estop/EStopManager.h"
#include "Logging.h"

#include <cstdint>

#include <esp_system.h>

using namespace OpenShock::MessageHandlers::Server;

using TriggerType = OpenShock::Serialization::Gateway::TriggerType;

void _Private::HandleTrigger(const OpenShock::Serialization::Gateway::GatewayToHubMessage* root)
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

  switch (triggerType) {
    case TriggerType::Restart:
      esp_restart();
      break;
    case TriggerType::EmergencyStop:
      EStopManager::Trigger();
      break;
    case TriggerType::CaptivePortalEnable:
      OpenShock::CaptivePortal::SetAlwaysEnabled(true);
      break;
    case TriggerType::CaptivePortalDisable:
      OpenShock::CaptivePortal::SetAlwaysEnabled(false);
      break;
    default:
      OS_LOGW(TAG, "Got unknown trigger type: %hhu", static_cast<uint8_t>(triggerType));
      break;
  }
}
