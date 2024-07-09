#include "event_handlers/WebSocket.h"

#include "event_handlers/impl/WSGateway.h"

#include "Logging.h"

#include "serialization/_fbs/GatewayToHubMessage_generated.h"

#include <WebSockets.h>

#include <array>
#include <cstdint>

static const char* TAG = "ServerMessageHandlers";

namespace Schemas  = OpenShock::Serialization::Gateway;
namespace Handlers = OpenShock::MessageHandlers::Server::_Private;
typedef Schemas::GatewayToHubMessagePayload PayloadType;

using namespace OpenShock;

const std::size_t HANDLER_COUNT = static_cast<std::size_t>(PayloadType::MAX) + 1;

#define SET_HANDLER(payload, handler) handlers[static_cast<std::size_t>(payload)] = handler

static std::array<Handlers::HandlerType, HANDLER_COUNT> s_serverHandlers = []() {
  std::array<Handlers::HandlerType, HANDLER_COUNT> handlers {};
  handlers.fill(Handlers::HandleInvalidMessage);

  SET_HANDLER(PayloadType::ShockerCommandList, Handlers::HandleShockerCommandList);
  SET_HANDLER(PayloadType::CaptivePortalConfig, Handlers::HandleCaptivePortalConfig);
  SET_HANDLER(PayloadType::OtaInstall, Handlers::HandleOtaInstall);

  return handlers;
}();

void EventHandlers::WebSocket::HandleGatewayBinary(const std::uint8_t* data, std::size_t len) {
  // Deserialize
  auto msg = flatbuffers::GetRoot<Schemas::GatewayToHubMessage>(data);
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
