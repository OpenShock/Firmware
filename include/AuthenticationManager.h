#pragma once

#include <WString.h>

#include <cstdint>

namespace ShockLink::AuthenticationManager {
  bool Authenticate(unsigned int pairCode);

  bool IsAuthenticated();
  String GetAuthToken();
  void ClearAuthToken();
}  // namespace ShockLink::AuthenticationManager
