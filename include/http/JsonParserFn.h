#pragma once

#include <cJSON.h>

#include <cstdint>
#include <functional>

namespace OpenShock::HTTP {
  template<typename T>
  using JsonParserFn = std::function<bool(const cJSON* json, T& data)>;
}
