#pragma once

#include "util/FunctionRef.h"

#include <cstdint>
#include <span>

namespace OpenShock::Serialization::Common {
  // Invoked synchronously with the finished buffer before the Serialize* call returns; never stored.
  using SerializationCallbackFn = OpenShock::Util::FunctionRef<bool(std::span<const uint8_t> data)>;
}
