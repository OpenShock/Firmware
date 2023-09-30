#pragma once

#include <WString.h>

#include <cstdint>

namespace OpenShock::AuthenticationManager {
  bool Authenticate(unsigned int pairCode);

  bool IsAuthenticated();
  String GetAuthToken();
  void ClearAuthToken();
}  // namespace OpenShock::AuthenticationManager
