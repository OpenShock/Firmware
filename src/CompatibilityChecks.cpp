#include "Common.h"
#include "Chipset.h"

const bool kIsValidOrUndefinedRfTxPin = OpenShock::IsValidOutputPin(OPENSHOCK_RF_TX_GPIO) || OPENSHOCK_RF_TX_GPIO == OPENSHOCK_GPIO_INVALID;
static_assert(kIsValidOrUndefinedRfTxPin , "OPENSHOCK_RF_TX_GPIO is not a valid output GPIO, and is not declared as bypassed by board specific definitions, refusing to compile");

const bool kIsValidOrUndefinedEStopPin = OpenShock::IsValidInputPin(OPENSHOCK_ESTOP_PIN) || OPENSHOCK_ESTOP_PIN == OPENSHOCK_GPIO_INVALID;
static_assert(kIsValidOrUndefinedEStopPin, "OPENSHOCK_ESTOP_PIN is not a valid input GPIO, and is not declared as bypassed by board specific definitions, refusing to compile");

#ifdef OPENSHOCK_LED_GPIO
static_assert(OpenShock::IsValidOutputPin(OPENSHOCK_LED_GPIO), "OPENSHOCK_LED_GPIO is not a valid output GPIO, and is not declared as bypassed by board specific definitions, refusing to compile");
#endif

#ifdef OPENSHOCK_LED_WS2812B
static_assert(OpenShock::IsValidOutputPin(OPENSHOCK_LED_WS2812B), "OPENSHOCK_LED_WS2812B is not a valid output GPIO, and is not declared as bypassed by board specific definitions, refusing to compile");
#endif
