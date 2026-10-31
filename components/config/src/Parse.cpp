#include "config/Parse.h"

const char* const TAG = "Config::Parse";

#include "Logging.h"

#include <cinttypes>
#include <cstdint>

using namespace OpenShock;

std::optional<bool> Config::Parse::Internal::CheckObject(JSON::JsonView json, const char* tag)
{
  if (!json.valid()) {
    OS_LOGW(tag, "Config is null, setting to default");
    return true;
  }
  if (!json.isObject()) {
    OS_LOGE(tag, "json is not an object");
    return false;
  }
  return std::nullopt;
}

bool Config::Parse::JsonBool(bool& out, JSON::JsonView json, std::string_view name, bool defaultVal, const char* tag)
{
  JSON::JsonView field = json[name];
  if (!field.valid()) {
    out = defaultVal;
    return true;
  }
  auto parsed = field.tryGetBool();
  if (!parsed) {
    OS_LOGE(tag, "%.*s is not a boolean", static_cast<int>(name.size()), name.data());
    return false;
  }
  out = *parsed;
  return true;
}

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
