#pragma once

#include <cstdint>
#include <functional>
#include <span>

namespace OpenShock::Serialization::Common {
  typedef std::function<bool(std::span<const uint8_t> data)> SerializationCallbackFn;
}
