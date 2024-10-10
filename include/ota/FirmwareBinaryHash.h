#pragma once

#include <cstdint>
#include <string>

namespace OpenShock
{
  struct FirmwareBinaryHash
  {
    std::string name;
    uint8_t hash[32];
  };
} // namespace OpenShock
