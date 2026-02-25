#include "message_handlers/WebSocket.h"

const char* const TAG = "ServerMessageHandlers";

#include "message_handlers/impl/WSGateway.h"

#include "Logging.h"

#include "serialization/_fbs/GatewayToHubMessage_generated.h"

#include <WebSockets.h>

#include <array>
#include <cstdint>

namespace Schemas  = OpenShock::Serialization::Gateway;
namespace Handlers = OpenShock::MessageHandlers::Server::_Private;
typedef Schemas::GatewayToHubMessagePayload PayloadType;

using namespace OpenShock;

const std::size_t HANDLER_COUNT = static_cast<std::size_t>(PayloadType::MAX) + 1;

static std::array<Handlers::HandlerType, HANDLER_COUNT> s_serverHandlers = []() {
  std::array<Handlers::HandlerType, HANDLER_COUNT> handlers {};
  handlers.fill(Handlers::HandleInvalidMessage);

  auto set = [&](PayloadType p, Handlers::HandlerType h) {
    handlers[static_cast<std::size_t>(p)] = h;
  };

  set(PayloadType::Ping, Handlers::HandlePing);
  set(PayloadType::Trigger, Handlers::HandleTrigger);
  set(PayloadType::ShockerCommandList, Handlers::HandleShockerCommandList);
  set(PayloadType::OtaUpdateRequest, Handlers::HandleOtaUpdateRequest);

  return handlers;
}();

void MessageHandlers::WebSocket::HandleGatewayBinary(tcb::span<const uint8_t> data)
{
  // Deserialize
  auto msg = flatbuffers::GetRoot<Schemas::GatewayToHubMessage>(data.data());
  if (msg == nullptr) {
    OS_LOGE(TAG, "Failed to deserialize message");
    return;
  }

  // Validate buffer
  flatbuffers::Verifier::Options verifierOptions {
    .max_size = 4096,  // TODO: Profile this
  };
  flatbuffers::Verifier verifier(data.data(), data.size(), verifierOptions);
  if (!msg->Verify(verifier)) {
    OS_LOGE(TAG, "Failed to verify message");
    return;
  }

  if (msg->payload_type() < PayloadType::MIN || msg->payload_type() > PayloadType::MAX) {
    Handlers::HandleInvalidMessage(msg);
    return;
  }

  s_serverHandlers[static_cast<std::size_t>(msg->payload_type())](msg);
}
