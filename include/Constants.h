#pragma once

#include <cstdint>

#ifndef OPENSHOCK_API_DOMAIN
#error "OPENSHOCK_API_DOMAIN must be defined"
#endif

#ifndef OPENSHOCK_API_BASE_URL
#define OPENSHOCK_API_BASE_URL "https://" OPENSHOCK_API_DOMAIN
#endif

#define OPENSHOCK_API_URL(path) OPENSHOCK_API_BASE_URL path

#ifndef OPENSHOCK_FW_VERSION
#error "OPENSHOCK_FW_VERSION must be defined"
#endif

#ifndef OPENSHOCK_RADIO_TX_GPIO
#warning "OPENSHOCK_RADIO_TX_GPIO is not defined, using default value of UINT8_MAX"
#define OPENSHOCK_RADIO_TX_GPIO UINT8_MAX
#endif

namespace OpenShock::Constants {
  constexpr std::uint8_t GPIO_INVALID  = UINT8_MAX;
  constexpr std::uint8_t GPIO_RADIO_TX = OPENSHOCK_RADIO_TX_GPIO;
}  // namespace OpenShock::Constants
