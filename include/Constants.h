#pragma once

#include <cstdint>

#ifndef SHOCKLINK_DOMAIN
#define SHOCKLINK_DOMAIN "shocklink.net"
#endif

#ifndef SHOCKLINK_API_BASE_URL
#define SHOCKLINK_API_BASE_URL "https://" SHOCKLINK_API_DOMAIN
#endif

#define SHOCKLINK_API_URL(path) SHOCKLINK_API_BASE_URL path

/*
    Constants

    This namespace contains commonly used constants.
    These are externed so that they can be used in multiple files without bloating the binary.

    Ref: https://esp32.com/viewtopic.php?t=8742
*/
namespace ShockLink::Constants {
  extern const char* const Version;
  extern const char* const ApiDomain;
}  // namespace ShockLink::Constants
