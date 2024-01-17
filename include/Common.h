#pragma once

#include <cstdint>

#define DISABLE_COPY(TypeName)             \
  TypeName(const TypeName&) = delete;      \
  void operator=(const TypeName&) = delete
#define DISABLE_MOVE(TypeName)             \
  TypeName(TypeName&&) = delete;           \
  void operator=(TypeName&&) = delete


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

#ifndef OPENSHOCK_GPIO_INVALID
#define OPENSHOCK_GPIO_INVALID 0
#endif

#ifndef OPENSHOCK_RF_TX_GPIO
#warning "OPENSHOCK_RF_TX_GPIO is not defined, setting to OPENSHOCK_GPIO_INVALID"
#define OPENSHOCK_RF_TX_GPIO OPENSHOCK_GPIO_INVALID
#endif

namespace OpenShock::Constants {
  constexpr std::uint8_t GPIO_INVALID  = OPENSHOCK_GPIO_INVALID;
  constexpr std::uint8_t GPIO_RF_TX = OPENSHOCK_RF_TX_GPIO;
}  // namespace OpenShock::Constants

