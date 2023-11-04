#include "MessageHandlers/Server.h"

#include "MessageHandlers/Server_Private.h"
#include "Logging.h"

#include "_fbs/ServerToDeviceMessage_generated.h"

#include <WebSockets.h>

#include <array>
#include <cstdint>

static const char* TAG = "ServerMessageHandlers";

namespace Schemas  = OpenShock::Serialization;
namespace Handlers = OpenShock::MessageHandlers::Server::_Private;
typedef Schemas::ServerToDeviceMessagePayload PayloadType;

using namespace OpenShock;

constexpr std::size_t HANDLER_COUNT = static_cast<std::size_t>(PayloadType::MAX) + 1;

#define SET_HANDLER(payload, handler) handlers[static_cast<std::size_t>(payload)] = handler

static std::array<Handlers::HandlerType, HANDLER_COUNT> s_serverHandlers = []() {
  std::array<Handlers::HandlerType, HANDLER_COUNT> handlers {};
  handlers.fill(Handlers::HandleInvalidMessage);

  SET_HANDLER(PayloadType::ShockerCommandList, Handlers::HandleShockerCommandList);
  SET_HANDLER(PayloadType::CaptivePortalConfig, Handlers::HandleCaptivePortalConfig);

  return handlers;
}();

void MessageHandlers::Server::HandleBinary(const std::uint8_t* data, std::size_t len) {
  // Deserialize
  auto msg = flatbuffers::GetRoot<Schemas::ServerToDeviceMessage>(data);
  if (msg == nullptr) {
    ESP_LOGE(TAG, "Failed to deserialize message");
    return;
  }

  // Validate buffer
  flatbuffers::Verifier::Options verifierOptions {
    .max_size = 4096,  // TODO: Profile this
  };
  flatbuffers::Verifier verifier(data, len, verifierOptions);
  if (!msg->Verify(verifier)) {
    ESP_LOGE(TAG, "Failed to verify message");
    return;
  }

  if (msg->payload_type() < PayloadType::MIN || msg->payload_type() > PayloadType::MAX) {
    Handlers::HandleInvalidMessage(msg);
    return;
  }

  s_serverHandlers[static_cast<std::size_t>(msg->payload_type())](msg);
}
