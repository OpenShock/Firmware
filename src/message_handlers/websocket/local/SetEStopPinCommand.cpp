#include "message_handlers/impl/WSLocal.h"

#include "captiveportal/Manager.h"
#include "Chipset.h"
#include "config/Config.h"
#include "estop/EStopManager.h"
#include "Logging.h"

#include <cstdint>

const char* const TAG = "LocalMessageHandlers";

static void serializeResult(uint8_t socketId, int8_t pin, OpenShock::Serialization::Local::SetGPIOResultCode result)
{
  flatbuffers::FlatBufferBuilder builder(1024);  // TODO: Profile this

  auto responseOffset = OpenShock::Serialization::Local::CreateSetEstopPinCommandResult(builder, pin, result);

  auto msg = OpenShock::Serialization::Local::CreateHubToLocalMessage(builder, OpenShock::Serialization::Local::HubToLocalMessagePayload::SetEstopPinCommandResult, responseOffset.Union());

  OpenShock::Serialization::Local::FinishHubToLocalMessageBuffer(builder, msg);

  OpenShock::CaptivePortal::SendMessageBIN(socketId, builder.GetBufferSpan());
}

using namespace OpenShock::MessageHandlers::Local;

static OpenShock::Serialization::Local::SetGPIOResultCode setEstopPin(int8_t pin)
{
  if (OpenShock::IsValidInputPin(pin)) {
    if (!OpenShock::EStopManager::SetEStopPin(static_cast<gpio_num_t>(pin))) {
      OS_LOGE(TAG, "Failed to set EStop pin");

      return OpenShock::Serialization::Local::SetGPIOResultCode::InternalError;
    }

    if (!OpenShock::Config::SetEStopGpioPin(static_cast<gpio_num_t>(pin))) {
      OS_LOGE(TAG, "Failed to set EStop pin in config");

      return OpenShock::Serialization::Local::SetGPIOResultCode::InternalError;
    }

    return OpenShock::Serialization::Local::SetGPIOResultCode::Success;
  } else {
    return OpenShock::Serialization::Local::SetGPIOResultCode::InvalidPin;
  }
}

void _Private::HandleSetEstopPinCommand(uint8_t socketId, const OpenShock::Serialization::Local::LocalToHubMessage* root)
{
  auto msg = root->payload_as_SetEstopPinCommand();
  if (msg == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as SetEstopPinCommand");
    return;
  }

  int8_t pin = msg->pin();

  auto result = setEstopPin(pin);

  serializeResult(socketId, pin, result);
}
