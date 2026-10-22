#include "config/internal/utils.h"

const char* const TAG = "Config::Internal::Utils";

#include "Chipset.h"
#include "Logging.h"

#include <cinttypes>
#include <cstdint>

using namespace OpenShock;

bool Config::Internal::Utils::FromIntGpioNum(gpio_num_t& val, int32_t intVal)
{
  if (intVal == GPIO_NUM_NC) {
    val = GPIO_NUM_NC;
    return true;
  }

  if (intVal < 0 || intVal >= GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO(intVal)) {
    OS_LOGE(TAG, "invalid GPIO number %" PRId32, intVal);
    return false;
  }

  val = static_cast<gpio_num_t>(intVal);

  return true;
}

void Config::Internal::Utils::FromFbsStr(std::string& str, const flatbuffers::String* fbsStr, const char* defaultStr)
{
  if (fbsStr != nullptr) {
    str = fbsStr->c_str();
  } else {
    str = defaultStr;
  }
}

bool Config::Internal::Utils::FromJsonGpioNum(gpio_num_t& val, JSON::JsonView json, std::string_view name)
{
  int32_t intVal;
  if (!json[name].tryGetI32(intVal)) {
    return false;
  }

  return FromIntGpioNum(val, intVal);
}
