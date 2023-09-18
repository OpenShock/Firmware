#pragma once

#include <WString.h>

#include <cstdint>

namespace ShockLink::AuthenticationManager
{
    bool Authenticate(std::uint32_t pairCode);

    bool IsAuthenticated();
    String GetAuthToken();
    void ClearAuthToken();
}