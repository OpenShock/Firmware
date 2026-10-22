#include "Chipset.h"

const char* const TAG = "Board";

#include "BoardDescriptor.h"
#include "Logging.h"
#include "OpenShock.h"

#include <cstring>

using namespace OpenShock;

namespace {
  bool checkPin(const char* name, int8_t pin, bool (*isValid)(int8_t))
  {
    if (pin == OPENSHOCK_GPIO_INVALID || isValid(pin)) return true;
    OS_LOGE(TAG, "%s is GPIO %hhi, which is not usable on this chip and not bypassed for this board", name, pin);
    return false;
  }
}  // namespace

bool OpenShock::ValidateBoardDescriptor()
{
  const BoardDescriptor& desc = Board::Get();

  bool ok = Board::IsStamped();

  if (std::strcmp(desc.chip, OPENSHOCK_FW_CHIP) != 0) {
    OS_LOGE(TAG, "Descriptor says chip %s but this firmware was built for " OPENSHOCK_FW_CHIP, desc.chip);
    ok = false;
  }

  ok &= checkPin("RF TX pin", desc.rfTxGpio, IsValidOutputPin);
  ok &= checkPin("E-Stop pin", desc.estopGpio, IsValidInputPin);
  ok &= checkPin("LED pin", desc.ledGpio, IsValidOutputPin);
  ok &= checkPin("WS2812B pin", desc.ws2812bGpio, IsValidOutputPin);

  return ok;
}
