#include "message_handlers/WebSocket.h"

const char* const TAG = "ServerMessageHandlers";

#include "message_handlers/impl/WSGateway.h"

#include "Logging.h"

#include "serialization/_fbs/GatewayToHubMessage_generated.h"
#include "serialization/VerifiedRoot.h"

#include <array>
#include <cstdint>
#include <utility>

namespace Schemas  = OpenShock::Serialization::Gateway;
namespace Handlers = OpenShock::MessageHandlers::Server::_Private;
using PayloadType  = Schemas::GatewayToHubMessagePayload;

using namespace OpenShock;

const std::size_t HANDLER_COUNT = std::to_underlying(PayloadType::MAX) + 1;

static std::array<Handlers::HandlerType, HANDLER_COUNT> s_serverHandlers = []() {
  std::array<Handlers::HandlerType, HANDLER_COUNT> handlers {};
  handlers.fill(Handlers::HandleInvalidMessage);

  auto set = [&](PayloadType p, Handlers::HandlerType h) {
    handlers[std::to_underlying(p)] = h;
  };

  set(PayloadType::Ping, Handlers::HandlePing);
  set(PayloadType::Trigger, Handlers::HandleTrigger);
  set(PayloadType::Common_ShockerCommandList, Handlers::HandleShockerCommandList);
  set(PayloadType::OtaUpdateRequest, Handlers::HandleOtaUpdateRequest);

  return handlers;
}();

void MessageHandlers::WebSocket::HandleGatewayBinary(GatewayClient& client, std::span<const uint8_t> data)
{
  auto msg = Serialization::GetVerifiedRoot<Schemas::GatewayToHubMessage>(data, MaxGatewayMessageSize);
  if (msg == nullptr) {
    OS_LOGE(TAG, "Invalid message (%zu bytes, max %zu)", data.size(), MaxGatewayMessageSize);
    return;
  }

  if (msg->payload_type() < PayloadType::MIN || msg->payload_type() > PayloadType::MAX) {
    Handlers::HandleInvalidMessage(client, msg);
    return;
  }

  s_serverHandlers[std::to_underlying(msg->payload_type())](client, msg);
}
