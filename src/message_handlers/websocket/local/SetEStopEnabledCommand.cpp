#include "message_handlers/impl/WSLocal.h"

#include "CaptivePortal.h"
#include "config/Config.h"
#include "EStopManager.h"
#include "Logging.h"

#include <cstdint>

const char* const TAG = "LocalMessageHandlers";

static void serializeResult(uint8_t socketId, bool enabled, bool success)
{
  flatbuffers::FlatBufferBuilder builder(1024);  // TODO: Profile this

  auto responseOffset = OpenShock::Serialization::Local::CreateSetEstopEnabledCommandResult(builder, enabled, success);

  auto msg = OpenShock::Serialization::Local::CreateHubToLocalMessage(builder, OpenShock::Serialization::Local::HubToLocalMessagePayload::SetEstopEnabledCommandResult, responseOffset.Union());

  OpenShock::Serialization::Local::FinishHubToLocalMessageBuffer(builder, msg);

  const uint8_t* buffer = builder.GetBufferPointer();
  uint8_t size          = builder.GetSize();

  OpenShock::CaptivePortal::SendMessageBIN(socketId, buffer, size);
}

using namespace OpenShock::MessageHandlers::Local;

static bool setEstopEnabled(bool enabled)
{
  if (!OpenShock::EStopManager::SetEStopEnabled(enabled)) {
    OS_LOGE(TAG, "Failed to set EStop enabled");

    return false;
  }

  if (!OpenShock::Config::SetEStopEnabled(enabled)) {
    OS_LOGE(TAG, "Failed to set EStop pin in config");

    return false;
  }

  return true;
}

void _Private::HandleSetEstopEnabledCommand(uint8_t socketId, const OpenShock::Serialization::Local::LocalToHubMessage* root)
{
  auto msg = root->payload_as_SetEstopEnabledCommand();
  if (msg == nullptr) {
    OS_LOGE(TAG, "Payload cannot be parsed as SetEstopEnabledCommand");
    return;
  }

  bool enabled = msg->enabled();

  bool success = setEstopEnabled(enabled);

  serializeResult(socketId, enabled, success);
}
