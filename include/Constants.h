#pragma once

#include <cstdint>

#ifndef OPENSHOCK_DOMAIN
#define OPENSHOCK_DOMAIN "shocklink.net"
#endif

#ifndef OPENSHOCK_API_BASE_URL
#define OPENSHOCK_API_BASE_URL "https://" OPENSHOCK_API_DOMAIN
#endif

#define OPENSHOCK_API_URL(path) OPENSHOCK_API_BASE_URL path

/*
    Constants

    This namespace contains commonly used constants.
    These are externed so that they can be used in multiple files without bloating the binary.

    Ref: https://esp32.com/viewtopic.php?t=8742
*/
namespace OpenShock::Constants {
  extern const char* const Version;
  extern const char* const ApiDomain;
}  // namespace OpenShock::Constants
