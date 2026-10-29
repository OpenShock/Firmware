// The board descriptor a host test suite runs with. In firmware this symbol is the
// placeholder in components/common/src/BoardDescriptorImage.cpp that the stamper
// overwrites; the host build is never stamped, so it is built here from the fixed test
// board in include/openshock_board.h (classic ESP32, RF TX on GPIO4, nothing else) and
// Board::Get() serves it like a stamped one. host_support compiles it, together with
// common/src/BoardDescriptor.cpp, for every suite.
#include "BoardDescriptor.h"
#include "openshock_board.h"

namespace {
  constexpr OpenShock::BoardDescriptor testDescriptor()
  {
    OpenShock::BoardDescriptor desc = OpenShock::Board::Defaults(OPENSHOCK_FW_BOARD, OPENSHOCK_FW_CHIP);
    desc.rfTxGpio                   = OPENSHOCK_RF_TX_GPIO;
    desc.estopGpio                  = OPENSHOCK_ESTOP_PIN;
    desc.ledGpio                    = OPENSHOCK_LED_GPIO;
    desc.ws2812bGpio                = OPENSHOCK_LED_WS2812B;
    desc.flags                      = OPENSHOCK_LED_SWAP_RG_CHANNELS ? OPENSHOCK_BOARD_FLAG_LED_SWAP_RG : 0u;
    desc.gpioBypassMask             = OPENSHOCK_GPIO_BYPASS_MASK;
    return OpenShock::Board::Seal(desc);
  }
}  // namespace

extern "C" const OpenShock::BoardDescriptor openshock_board_desc;
extern "C" constinit const OpenShock::BoardDescriptor openshock_board_desc = testDescriptor();
