#pragma once

#include <WebSockets.h>

#include <cstdint>

namespace OpenShock::MessageHandlers::Local {
  void Handle(std::uint8_t socketId, WStype_t type, const std::uint8_t* data, std::size_t len);
}
