#include "config/internal/utils.h"

const char* const TAG = "Config::Internal::Utils";

#include "Chipset.h"
#include "Logging.h"

#include <cinttypes>
#include <cstdint>

using namespace OpenShock;

std::optional<gpio_num_t> Config::Internal::Utils::FromIntGpioNum(int32_t intVal)
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

void Config::Internal::Utils::FromFbsStr(std::string& str, const flatbuffers::String* fbsStr, const char* defaultStr)
{
  if (fbsStr != nullptr) {
    str = fbsStr->c_str();
  } else {
    str = defaultStr;
  }
}

std::optional<gpio_num_t> Config::Internal::Utils::FromJsonGpioNum(JSON::JsonView json, std::string_view name)
{
  auto intVal = json[name].tryGetI32();
  if (!intVal) {
    return std::nullopt;
  }

  return FromIntGpioNum(*intVal);
}
