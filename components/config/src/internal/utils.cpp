#include "config/internal/utils.h"

const char* const TAG = "Config::Internal::Utils";

#include "Chipset.h"
#include "Logging.h"

#include <cstdint>

using namespace OpenShock;

bool Config::Internal::Utils::FromU8GpioNum(gpio_num_t& val, uint8_t u8Val)
{
  if (u8Val >= GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO(u8Val)) {
    OS_LOGE(TAG, "invalid GPIO number");
    return false;
  }

  val = static_cast<gpio_num_t>(u8Val);

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
  uint8_t u8Val;
  if (!json[name].tryGetU8(u8Val)) {
    return false;
  }

  return FromU8GpioNum(val, u8Val);
}
