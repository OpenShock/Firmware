#pragma once

#include <cstdint>
#include <functional>

class cJSON;

namespace OpenShock::HTTP {
  template<typename T>
  using JsonParserFn = std::function<bool(const cJSON* json, T& data)>;
}
