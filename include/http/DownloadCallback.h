#pragma once

#include <cstdint>
#include <functional>

namespace OpenShock::HTTP {
  using DownloadCallback = std::function<bool(std::size_t offset, const uint8_t* data, std::size_t len)>;
}
