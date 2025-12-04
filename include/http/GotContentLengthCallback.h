#pragma once

#include <cstdint>
#include <functional>

namespace OpenShock::HTTP {
  using GotContentLengthCallback = std::function<bool(int contentLength)>;
}
