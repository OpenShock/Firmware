#pragma once

#include <cstdint>

#ifndef OPENSHOCK_API_DOMAIN
#error "OPENSHOCK_API_DOMAIN must be defined"
#endif
#ifndef OPENSHOCK_API_BASE_URL
#define OPENSHOCK_API_BASE_URL "https://" OPENSHOCK_API_DOMAIN
#endif

#ifndef OPENSHOCK_FW_CDN_DOMAIN
#error "OPENSHOCK_FW_CDN_DOMAIN must be defined"
#endif
#ifndef OPENSHOCK_FW_CDN_BASE_URL
#define OPENSHOCK_FW_CDN_BASE_URL "https://" OPENSHOCK_FW_CDN_DOMAIN
#endif

#define OPENSHOCK_API_URL(path) OPENSHOCK_API_BASE_URL path
#define OPENSHOCK_FW_CDN_URL(path) OPENSHOCK_FW_CDN_BASE_URL path

#ifndef OPENSHOCK_FW_VERSION
#error "OPENSHOCK_FW_VERSION must be defined"
#endif

#ifndef OPENSHOCK_RF_TX_GPIO
#warning "OPENSHOCK_RF_TX_GPIO is not defined, using default value of UINT8_MAX"
#define OPENSHOCK_RF_TX_GPIO UINT8_MAX
#endif

namespace OpenShock::Constants {
  constexpr std::uint8_t GPIO_INVALID  = UINT8_MAX;
  constexpr std::uint8_t GPIO_RF_TX = OPENSHOCK_RF_TX_GPIO;
}  // namespace OpenShock::Constants
