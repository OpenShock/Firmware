#include "message_handlers/WebSocket.h"

const char* const TAG = "LocalMessageHandlers";

#include "Logging.h"
#include "message_handlers/impl/WSLocal.h"

#include "serialization/_fbs/LocalToHubMessage_generated.h"
#include "serialization/VerifiedRoot.h"

#include <array>
#include <cstdint>
#include <utility>

namespace Schemas  = OpenShock::Serialization::Local;
namespace Handlers = OpenShock::MessageHandlers::Local::_Private;
using PayloadType  = Schemas::LocalToHubMessagePayload;

using namespace OpenShock;

const std::size_t HANDLER_COUNT = std::to_underlying(PayloadType::MAX) + 1;

#define SET_HANDLER(payload) handlers[std::to_underlying(PayloadType::payload)] = Handlers::Handle##payload

static std::array<Handlers::HandlerType, HANDLER_COUNT> s_localHandlers = []() {
  std::array<Handlers::HandlerType, HANDLER_COUNT> handlers {};
  handlers.fill(Handlers::HandleInvalidMessage);

  SET_HANDLER(Common_ShockerCommandList);

  return handlers;
}();

void MessageHandlers::WebSocket::HandleLocalBinary(uint8_t socketId, std::span<const uint8_t> data)
{
  auto msg = Serialization::GetVerifiedRoot<Schemas::LocalToHubMessage>(data, CONFIG_OPENSHOCK_LOCAL_MAX_MESSAGE_SIZE);
  if (msg == nullptr) {
    OS_LOGE(TAG, "Invalid message (%zu bytes, max %d)", data.size(), CONFIG_OPENSHOCK_LOCAL_MAX_MESSAGE_SIZE);
    return;
  }

  if (msg->payload_type() < PayloadType::MIN || msg->payload_type() > PayloadType::MAX) {
    Handlers::HandleInvalidMessage(socketId, msg);
    return;
  }

  s_localHandlers[std::to_underlying(msg->payload_type())](socketId, msg);
}
