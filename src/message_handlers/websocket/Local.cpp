#include "message_handlers/WebSocket.h"

const char* const TAG = "LocalMessageHandlers";

#include "Logging.h"
#include "message_handlers/impl/WSLocal.h"

#include "serialization/_fbs/LocalToHubMessage_generated.h"

#include <WebSockets.h>

#include <array>
#include <cstdint>

namespace Schemas  = OpenShock::Serialization::Local;
namespace Handlers = OpenShock::MessageHandlers::Local::_Private;
typedef Schemas::LocalToHubMessagePayload PayloadType;

using namespace OpenShock;

const std::size_t HANDLER_COUNT = static_cast<std::size_t>(PayloadType::MAX) + 1;

#define SET_HANDLER(payload) handlers[static_cast<std::size_t>(PayloadType::payload)] = Handlers::Handle##payload

static std::array<Handlers::HandlerType, HANDLER_COUNT> s_localHandlers = []() {
  std::array<Handlers::HandlerType, HANDLER_COUNT> handlers {};
  handlers.fill(Handlers::HandleInvalidMessage);

  SET_HANDLER(Common_ShockerCommandList);

  return handlers;
}();

void MessageHandlers::WebSocket::HandleLocalBinary(uint8_t socketId, tcb::span<const uint8_t> data)
{
  if (data.size() < sizeof(flatbuffers::uoffset_t)) {
    OS_LOGE(TAG, "Message too small to be a valid FlatBuffer");
    return;
  }

  // The Verifier asserts size < max_size, so oversize input must be rejected before constructing one
  constexpr std::size_t MaxMessageSize = 4096;
  if (data.size() >= MaxMessageSize) {
    OS_LOGE(TAG, "Message too large to be valid (%zu bytes)", data.size());
    return;
  }

  // Validate buffer
  flatbuffers::Verifier::Options verifierOptions {
    .max_size = MaxMessageSize,
  };
  flatbuffers::Verifier verifier(data.data(), data.size(), verifierOptions);
  if (!verifier.VerifyBuffer<Schemas::LocalToHubMessage>()) {
    OS_LOGE(TAG, "Failed to verify message");
    return;
  }

  // Deserialize (safe after verification)
  auto msg = flatbuffers::GetRoot<Schemas::LocalToHubMessage>(data.data());

  if (msg->payload_type() < PayloadType::MIN || msg->payload_type() > PayloadType::MAX) {
    Handlers::HandleInvalidMessage(socketId, msg);
    return;
  }

  s_localHandlers[static_cast<std::size_t>(msg->payload_type())](socketId, msg);
}
