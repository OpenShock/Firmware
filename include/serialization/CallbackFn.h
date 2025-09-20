#pragma once

#include <cstdint>
#include <functional>

#include "span.h"

namespace OpenShock::Serialization::Common {
  typedef std::function<bool(tcb::span<const uint8_t> data)> SerializationCallbackFn;
}
