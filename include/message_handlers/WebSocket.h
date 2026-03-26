#pragma once

#include "span.h"

#include <cstdint>

namespace OpenShock::MessageHandlers::WebSocket {
  void HandleGatewayBinary(tcb::span<const uint8_t> data);
  void HandleLocalBinary(uint8_t socketId, tcb::span<const uint8_t> data);
}
