#pragma once

#include <cstdint>


/*
    Constants

    This namespace contains commonly used constants.
    These are externed so that they can be used in multiple files without bloating the binary.

    Ref: https://esp32.com/viewtopic.php?t=8742
*/
namespace ShockLink::Constants
{
    extern const char *const Version;
    extern const char *const ApiDomain;
    extern const char *const DevApiDomain;
    extern const char *const ApiUrl;
    extern const char *const ApiPairUrl;
    extern const char *const ApiPairCodeUrl;
}