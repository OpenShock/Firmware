#include "Chipset.h"

const char* const TAG = "Board";

#include "BoardDescriptor.h"
#include "Logging.h"
#include "OpenShock.h"

#include <cstring>
#include <iterator>

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

  const struct {
    const char* name;
    int8_t pin;
    bool (*isValid)(int8_t);
  } pins[] = {
    {  "RF TX pin",    desc.rfTxGpio, IsValidOutputPin},
    { "E-Stop pin",   desc.estopGpio,  IsValidInputPin},
    {    "LED pin",     desc.ledGpio, IsValidOutputPin},
    {"WS2812B pin", desc.ws2812bGpio, IsValidOutputPin},
  };

  for (const auto& p : pins) {
    ok &= checkPin(p.name, p.pin, p.isValid);
  }

  // Each assigned pin must be its own GPIO. Two features on one pin - the RF output driving the E-Stop input, say -
  // is a wiring error the stamp should never encode, and nothing downstream checks it at boot.
  for (std::size_t i = 0; i < std::size(pins); i++) {
    if (pins[i].pin == OPENSHOCK_GPIO_INVALID) continue;
    for (std::size_t j = i + 1; j < std::size(pins); j++) {
      if (pins[j].pin == pins[i].pin) {
        OS_LOGE(TAG, "%s and %s are both GPIO %hhi", pins[i].name, pins[j].name, pins[i].pin);
        ok = false;
      }
    }
  }

  return ok;
}
