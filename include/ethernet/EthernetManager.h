#pragma once

#include <cstddef>

namespace OpenShock::EthernetManager {
  bool Init();
  bool IsLinkUp();
  bool HasIPAddress();
  bool GetIPv4(char* out, std::size_t len);
}  // namespace OpenShock::EthernetManager
