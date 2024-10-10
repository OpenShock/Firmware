#pragma once

#include <cstdint>
#include <string>

namespace OpenShock {
  struct FirmwareReleaseInfo {
    std::string appBinaryUrl;
    uint8_t appBinaryHash[32];
    std::string filesystemBinaryUrl;
    uint8_t filesystemBinaryHash[32];
  };
}  // namespace OpenShock
