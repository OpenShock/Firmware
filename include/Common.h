#pragma once

#include <cstdint>
#include <string_view>

#define DISABLE_COPY(TypeName)             \
  TypeName(const TypeName&) = delete;      \
  void operator=(const TypeName&) = delete
#define DISABLE_MOVE(TypeName)             \
  TypeName(TypeName&&) = delete;           \
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
#define OPENSHOCK_FW_USERAGENT OPENSHOCK_FW_HOSTNAME "/" OPENSHOCK_FW_VERSION " (arduino-esp32; " OPENSHOCK_FW_BOARD "; " OPENSHOCK_FW_CHIP "; Espressif)"
#endif

// Check if Arduino.h exists, if not instruct the developer to remove "arduino-esp32" from the useragent and replace it with "ESP-IDF", after which the developer may remove this warning.
#if defined(__has_include) && !__has_include("Arduino.h")
#warning "Let it be known that Arduino hath finally been cast aside in favor of the noble ESP-IDF! I beseech thee, kind sir or madam, wouldst thou kindly partake in the honors of expunging 'arduino-esp32' from yonder useragent aloft, and in its stead, bestow the illustrious 'ESP-IDF'?"
#endif

#if __cplusplus >= 202'302L
#warning "C++23 compiler detected"
#elif __cplusplus >= 202'002L
#warning "C++20 compiler detected"
#elif __cplusplus >= 201'703L
// C++17 :3
#elif __cplusplus >= 201'402L
#error "C++14 compiler detected, OpenShock requires a C++17 compliant compiler"
#elif __cplusplus >= 201'103L
#error "C++11 compiler detected, OpenShock requires a C++17 compliant compiler"
#elif __cplusplus >= 199'711L
#error "C++98 compiler detected, OpenShock requires a C++17 compliant compiler"
#elif __cplusplus == 1
#error "Pre-C++98 compiler detected, OpenShock requires a C++17 compliant compiler"
#else
#error "Unknown C++ standard detected, OpenShock requires a C++17 compliant compiler"
#endif

namespace OpenShock::Constants {
  const char* const FW_USERAGENT = OPENSHOCK_FW_USERAGENT;
}  // namespace OpenShock::Constants
