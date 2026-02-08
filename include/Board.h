#pragma once

#include <cstdint>

#define OPENSHOCK_GPIO_INVALID -1

namespace OpenShock::Constants {
    extern const char* const FW_USERAGENT;
    namespace Gpio {
        extern const int8_t RfTxPin;
        extern const int8_t EStopPin;
    }
}