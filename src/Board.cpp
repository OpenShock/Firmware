#include "Board.h"
#include "Common.h"
#include "Chipset.h"

#ifndef OPENSHOCK_FW_VERSION
#error "OPENSHOCK_FW_VERSION must be defined"
#endif

// Check if OPENSHOCK_FW_USERAGENT is overridden trough compiler flags, if not, generate a default useragent.
#ifndef OPENSHOCK_FW_USERAGENT
#define OPENSHOCK_FW_USERAGENT OPENSHOCK_FW_HOSTNAME "/" OPENSHOCK_FW_VERSION " (arduino-esp32; " OPENSHOCK_FW_BOARD "; " OPENSHOCK_FW_CHIP "; Espressif)"
#endif

#ifndef OPENSHOCK_RF_TX_GPIO
#warning "OPENSHOCK_RF_TX_GPIO is not defined, setting to OPENSHOCK_GPIO_INVALID"
#define OPENSHOCK_RF_TX_GPIO OPENSHOCK_GPIO_INVALID
#endif

#ifndef OPENSHOCK_ESTOP_PIN
#define OPENSHOCK_ESTOP_PIN OPENSHOCK_GPIO_INVALID
#endif

constexpr bool kIsValidOrUndefinedRfTxPin = OpenShock::IsValidOutputPin(OPENSHOCK_RF_TX_GPIO) || OPENSHOCK_RF_TX_GPIO == OPENSHOCK_GPIO_INVALID;
static_assert(kIsValidOrUndefinedRfTxPin , "OPENSHOCK_RF_TX_GPIO is not a valid output GPIO, and is not declared as bypassed by board specific definitions, refusing to compile");

constexpr bool kIsValidOrUndefinedEStopPin = OpenShock::IsValidInputPin(OPENSHOCK_ESTOP_PIN) || OPENSHOCK_ESTOP_PIN == OPENSHOCK_GPIO_INVALID;
static_assert(kIsValidOrUndefinedEStopPin, "OPENSHOCK_ESTOP_PIN is not a valid input GPIO, and is not declared as bypassed by board specific definitions, refusing to compile");

#ifdef OPENSHOCK_LED_GPIO
static_assert(OpenShock::IsValidOutputPin(OPENSHOCK_LED_GPIO), "OPENSHOCK_LED_GPIO is not a valid output GPIO, and is not declared as bypassed by board specific definitions, refusing to compile");
#endif

#ifdef OPENSHOCK_LED_WS2812B
static_assert(OpenShock::IsValidOutputPin(OPENSHOCK_LED_WS2812B), "OPENSHOCK_LED_WS2812B is not a valid output GPIO, and is not declared as bypassed by board specific definitions, refusing to compile");
#endif

const char* const OpenShock::Constants::FW_USERAGENT = OPENSHOCK_FW_USERAGENT;
const int8_t OpenShock::Constants::Gpio::RfTxPin = (gpio_num_t)123;
const int8_t OpenShock::Constants::Gpio::EStopPin = (gpio_num_t)123;