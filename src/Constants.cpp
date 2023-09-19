#include "Constants.h"

#define SHOCKLINK_DOMAIN "shocklink.net"

#define SHOCKLINK_API_DOMAIN "api." SHOCKLINK_DOMAIN
#define SHOCKLINK_DEV_API_DOMAIN "dev-api." SHOCKLINK_DOMAIN

#define SHOCKLINK_API_URL "https://" SHOCKLINK_API_DOMAIN

namespace ShockLink::Constants
{
    const char *const Version = "0.8.0";
    const char *const ApiDomain = SHOCKLINK_API_DOMAIN;
    const char *const DevApiDomain = SHOCKLINK_DEV_API_DOMAIN;
    const char *const ApiUrl = SHOCKLINK_API_URL;
    const char *const ApiPairUrl = SHOCKLINK_API_URL "/1/device/pair/";
    const char *const ApiPairCodeUrl = SHOCKLINK_API_URL "/1/device/self/";
}