#pragma once

#include <cstdint>

#define DISABLE_COPY(TypeName)              \
  TypeName(const TypeName&)       = delete; \
  void operator=(const TypeName&) = delete
#define DISABLE_MOVE(TypeName)         \
  TypeName(TypeName&&)       = delete; \
  void operator=(TypeName&&) = delete

#ifndef OPENSHOCK_API_DOMAIN
#error "OPENSHOCK_API_DOMAIN must be defined"
#endif
#ifndef OPENSHOCK_FW_CDN_DOMAIN
#error "OPENSHOCK_FW_CDN_DOMAIN must be defined"
#endif
#ifndef OPENSHOCK_FW_VERSION
#error "OPENSHOCK_FW_VERSION must be defined"
#endif

#define OPENSHOCK_API_URL(path)    "https://" OPENSHOCK_API_DOMAIN path
#define OPENSHOCK_FW_CDN_URL(path) "https://" OPENSHOCK_FW_CDN_DOMAIN path

#define OPENSHOCK_GPIO_INVALID 0

#ifndef OPENSHOCK_RF_TX_GPIO
#warning "OPENSHOCK_RF_TX_GPIO is not defined, setting to OPENSHOCK_GPIO_INVALID"
#define OPENSHOCK_RF_TX_GPIO OPENSHOCK_GPIO_INVALID
#endif

// Check if OPENSHOCK_FW_USERAGENT is overridden trough compiler flags, if not, generate a default useragent.
#ifndef OPENSHOCK_FW_USERAGENT
#define OPENSHOCK_FW_USERAGENT OPENSHOCK_FW_HOSTNAME "/" OPENSHOCK_FW_VERSION " (esp-idf; " OPENSHOCK_FW_BOARD "; " OPENSHOCK_FW_CHIP "; Espressif)"
#endif

namespace OpenShock::Constants {
  const std::uint8_t GPIO_INVALID = OPENSHOCK_GPIO_INVALID;
  const std::uint8_t GPIO_RF_TX   = OPENSHOCK_RF_TX_GPIO;
  const char* const FW_USERAGENT  = OPENSHOCK_FW_USERAGENT;
}  // namespace OpenShock::Constants
