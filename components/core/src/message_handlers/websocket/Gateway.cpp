#include "message_handlers/WebSocket.h"

const char* const TAG = "ServerMessageHandlers";

#include "message_handlers/impl/WSGateway.h"

#include "Logging.h"

#include "serialization/_fbs/GatewayToHubMessage_generated.h"

#include <array>
#include <cstdint>

namespace Schemas  = OpenShock::Serialization::Gateway;
namespace Handlers = OpenShock::MessageHandlers::Server::_Private;
typedef Schemas::GatewayToHubMessagePayload PayloadType;

static constexpr std::size_t MAX_MESSAGE_SIZE = 4096;  // TODO: Profile this

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
  if (data.size() < sizeof(flatbuffers::uoffset_t)) {
    OS_LOGE(TAG, "Message too small to be a valid FlatBuffer");
    return;
  }

  // Checked before constructing the Verifier: its constructor asserts
  // size < max_size, so an oversized message would abort instead of being rejected.
  if (data.size() > MAX_MESSAGE_SIZE) {
    OS_LOGE(TAG, "Message too large (%zu bytes, max %zu)", data.size(), MAX_MESSAGE_SIZE);
    return;
  }

  // Validate buffer
  flatbuffers::Verifier::Options verifierOptions {
    .max_size = MAX_MESSAGE_SIZE + 1,  // Verifier requires size < max_size
  };
  flatbuffers::Verifier verifier(data.data(), data.size(), verifierOptions);
  if (!verifier.VerifyBuffer<Schemas::GatewayToHubMessage>()) {
    OS_LOGE(TAG, "Failed to verify message");
    return;
  }

  // Deserialize (safe after verification)
  auto msg = flatbuffers::GetRoot<Schemas::GatewayToHubMessage>(data.data());

  if (msg->payload_type() < PayloadType::MIN || msg->payload_type() > PayloadType::MAX) {
    Handlers::HandleInvalidMessage(msg);
    return;
  }

  s_serverHandlers[static_cast<std::size_t>(msg->payload_type())](msg);
}
