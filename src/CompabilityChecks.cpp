#include "Constants.h"
#include "Chipset.h"

constexpr bool kIsValidRfTxPin = OpenShock::IsValidOutputPin(OPENSHOCK_RF_TX_GPIO) || OPENSHOCK_RF_TX_GPIO == UINT8_MAX;
static_assert(kIsValidRfTxPin , "OPENSHOCK_RF_TX_GPIO is not a valid output GPIO, and is not declared as bypassed by board specific definitions, refusing to compile");

#ifdef OPENSHOCK_LED_GPIO
static_assert(OpenShock::IsValidOutputPin(OPENSHOCK_LED_GPIO), "OPENSHOCK_LED_GPIO is not a valid output GPIO, and is not declared as bypassed by board specific definitions, refusing to compile");
#endif

#ifdef OPENSHOCK_LED_WS2812B
static_assert(OpenShock::IsValidOutputPin(OPENSHOCK_LED_WS2812B), "OPENSHOCK_LED_WS2812B is not a valid output GPIO, and is not declared as bypassed by board specific definitions, refusing to compile");
#endif
