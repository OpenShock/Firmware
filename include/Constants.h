#pragma once

#include <cstdint>

#define SHOCKLINK_DOMAIN "shocklink.net"

#define SHOCKLINK_API_DOMAIN     "api." SHOCKLINK_DOMAIN
#define SHOCKLINK_DEV_API_DOMAIN "dev-api." SHOCKLINK_DOMAIN

#define SHOCKLINK_API_BASE_URL  "https://" SHOCKLINK_API_DOMAIN
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
  extern const char* const DevApiDomain;
}  // namespace ShockLink::Constants
