#pragma once

#include <WebSockets.h>

#include <cstdint>

namespace OpenShock::MessageHandlers::Server {
  void Handle(WStype_t type, const std::uint8_t* data, std::size_t len);
}
