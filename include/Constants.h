#pragma once

#include <cstdint>

#define __SHOCKLINK_API_DOMAIN "api.shocklink.net"
#define __SHOCKLINK_API_URL "https://" __SHOCKLINK_API_DOMAIN
#define __SHOCKLINK_API_PAIR_URL __SHOCKLINK_API_URL "/1/device/pair/"
#define __SHOCKLINK_API_PAIR_CODE_URL __SHOCKLINK_API_URL "/1/device/self/"

namespace ShockLink::Constants
{
    static const char *const ApiDomain = __SHOCKLINK_API_DOMAIN;
    static const char *const ApiUrl = __SHOCKLINK_API_URL;
    static const char *const ApiPairUrl = __SHOCKLINK_API_PAIR_URL;
    static const char *const ApiPairCodeUrl = __SHOCKLINK_API_PAIR_CODE_URL;
}