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

#define OPENSHOCK_API_URL(path) OPENSHOCK_API_BASE_URL path

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
