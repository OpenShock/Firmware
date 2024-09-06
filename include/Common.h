#pragma once

#include "StringView.h"

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

#if __cplusplus >= 202'302L
#warning "C++23 compiler detected"
#elif __cplusplus >= 202'002L
// C++20 :3
#elif __cplusplus >= 201'703L
#warning "C++17 compiler detected, OpenShock requires a C++20 compliant compiler"
#elif __cplusplus >= 201'402L
#error "C++14 compiler detected, OpenShock requires a C++20 compliant compiler"
#elif __cplusplus >= 201'103L
#error "C++11 compiler detected, OpenShock requires a C++20 compliant compiler"
#elif __cplusplus >= 199'711L
#error "C++98 compiler detected, OpenShock requires a C++20 compliant compiler"
#elif __cplusplus == 1
#error "Pre-C++98 compiler detected, OpenShock requires a C++20 compliant compiler"
#else
#error "Unknown C++ standard detected, OpenShock requires a C++20 compliant compiler"
#endif

namespace OpenShock::Constants {
  const char* const FW_USERAGENT   = OPENSHOCK_FW_USERAGENT;
  const StringView FW_USERAGENT_sv = OPENSHOCK_FW_USERAGENT ""_sv;
}  // namespace OpenShock::Constants
