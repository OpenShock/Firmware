#include "message_handlers/WebSocket.h"

const char* const TAG = "ServerMessageHandlers";

#include "message_handlers/impl/WSGateway.h"

#include "Logging.h"

#include "serialization/_fbs/GatewayToHubMessage_generated.h"
#include "serialization/VerifiedRoot.h"

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
  set(PayloadType::Common_ShockerCommandList, Handlers::HandleShockerCommandList);
  set(PayloadType::OtaUpdateRequest, Handlers::HandleOtaUpdateRequest);

  return handlers;
}();

void MessageHandlers::WebSocket::HandleGatewayBinary(std::span<const uint8_t> data)
{
  auto msg = Serialization::GetVerifiedRoot<Schemas::GatewayToHubMessage>(data, MaxGatewayMessageSize);
  if (msg == nullptr) {
    OS_LOGE(TAG, "Invalid message (%zu bytes, max %zu)", data.size(), MaxGatewayMessageSize);
    return;
  }

  if (msg->payload_type() < PayloadType::MIN || msg->payload_type() > PayloadType::MAX) {
    Handlers::HandleInvalidMessage(msg);
    return;
  }

  s_serverHandlers[static_cast<std::size_t>(msg->payload_type())](msg);
}
