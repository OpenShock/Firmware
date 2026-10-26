#include "config/Parse.h"

const char* const TAG = "Config::Parse";

#include "Logging.h"

#include <cinttypes>
#include <cstdint>

using namespace OpenShock;

std::optional<gpio_num_t> Config::Parse::GpioNum(int32_t intVal)
{
  if (intVal == GPIO_NUM_NC) {
    return GPIO_NUM_NC;
  }

  if (intVal < 0 || intVal >= GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO(intVal)) {
    OS_LOGE(TAG, "invalid GPIO number %" PRId32, intVal);
    return std::nullopt;
  }

  return static_cast<gpio_num_t>(intVal);
}

std::optional<gpio_num_t> Config::Parse::JsonGpioNum(JSON::JsonView json, std::string_view name)
{
  auto intVal = json[name].tryGetI32();
  if (!intVal) {
    return std::nullopt;
  }

  return GpioNum(*intVal);
}
