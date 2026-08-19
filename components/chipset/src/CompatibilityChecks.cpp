#include "Chipset.h"
#include "OpenShock.h"

// Board pins are Kconfig options (CONFIG_OPENSHOCK_*) and always have a value
// (-1 == OPENSHOCK_GPIO_INVALID when the board has no such pin). A pin is valid
// if it's a usable GPIO or explicitly bypassed with INVALID; anything else is a
// misconfiguration and fails the build.
const bool kIsValidOrUndefinedRfTxPin = OpenShock::IsValidOutputPin(OPENSHOCK_RF_TX_GPIO) || OPENSHOCK_RF_TX_GPIO == OPENSHOCK_GPIO_INVALID;
static_assert(kIsValidOrUndefinedRfTxPin, "OPENSHOCK_RF_TX_GPIO is not a valid output GPIO, and is not declared as bypassed by board specific definitions, refusing to compile");

const bool kIsValidOrUndefinedEStopPin = OpenShock::IsValidInputPin(OPENSHOCK_ESTOP_PIN) || OPENSHOCK_ESTOP_PIN == OPENSHOCK_GPIO_INVALID;
static_assert(kIsValidOrUndefinedEStopPin, "OPENSHOCK_ESTOP_PIN is not a valid input GPIO, and is not declared as bypassed by board specific definitions, refusing to compile");

const bool kIsValidOrUndefinedLedPin = OpenShock::IsValidOutputPin(OPENSHOCK_LED_GPIO) || OPENSHOCK_LED_GPIO == OPENSHOCK_GPIO_INVALID;
static_assert(kIsValidOrUndefinedLedPin, "OPENSHOCK_LED_GPIO is not a valid output GPIO, and is not declared as bypassed by board specific definitions, refusing to compile");

const bool kIsValidOrUndefinedWs2812bPin = OpenShock::IsValidOutputPin(OPENSHOCK_LED_WS2812B) || OPENSHOCK_LED_WS2812B == OPENSHOCK_GPIO_INVALID;
static_assert(kIsValidOrUndefinedWs2812bPin, "OPENSHOCK_LED_WS2812B is not a valid output GPIO, and is not declared as bypassed by board specific definitions, refusing to compile");
