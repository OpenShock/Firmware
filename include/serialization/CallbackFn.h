#pragma once

#include <cstdint>
#include <functional>

namespace OpenShock::Serialization::Common {
  typedef std::function<bool(const std::uint8_t* data, std::size_t len)> SerializationCallbackFn;
}
