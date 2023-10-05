#pragma once

#include <WString.h>

#include <cstdint>

namespace OpenShock::AuthenticationManager {
  bool IsPaired();
  bool Pair(unsigned int pairCode);
  void UnPair();

  String GetAuthToken();
  void ClearAuthToken();
}  // namespace OpenShock::AuthenticationManager
