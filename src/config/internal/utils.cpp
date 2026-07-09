#include "config/internal/utils.h"

const char* const TAG = "Config::Internal::Utils";

#include "Chipset.h"
#include "Logging.h"
#include "util/IPAddressUtils.h"

#include <cstdint>

using namespace OpenShock;

template<typename T>
static bool utilFromJsonInt(T& val, JSON::JsonView json, std::string_view name, T defaultVal, int minVal, int maxVal)
{
  static_assert(std::is_integral_v<T>, "T must be an integral type");

  JSON::JsonView jsonVal = json[name];
  if (!jsonVal.valid()) {
    return false;
  }

  int64_t intVal;
  if (!jsonVal.tryGetI64(intVal)) {
    OS_LOGE(TAG, "value at '%.*s' is not a number", name.length(), name.data());
    return false;
  }

  if (intVal < minVal) {
    OS_LOGE(TAG, "value at '%.*s' is less than %d", name.length(), name.data(), minVal);
    return false;
  }

  if (intVal > maxVal) {
    OS_LOGE(TAG, "value at '%.*s' is greater than %d", name.length(), name.data(), maxVal);
    return false;
  }

  val = static_cast<T>(intVal);

  return true;
}

bool Config::Internal::Utils::FromU8GpioNum(gpio_num_t& val, uint8_t u8Val)
{
  if (u8Val >= GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO(u8Val)) {
    OS_LOGE(TAG, "invalid GPIO number");
    return false;
  }

  val = static_cast<gpio_num_t>(u8Val);

  return true;
}

void Config::Internal::Utils::FromU8GpioNum(gpio_num_t& val, uint8_t u8Val, gpio_num_t defaultVal)
{
  if (!FromU8GpioNum(val, u8Val)) {
    val = defaultVal;
  }
}

void Config::Internal::Utils::FromFbsStr(std::string& str, const flatbuffers::String* fbsStr, const char* defaultStr)
{
  if (fbsStr != nullptr) {
    str = fbsStr->c_str();
  } else {
    str = defaultStr;
  }
}

bool Config::Internal::Utils::FromFbsIPAddress(IPAddress& ip, const flatbuffers::String* fbsIP, const IPAddress& defaultIP)
{
  if (fbsIP == nullptr) {
    ip = defaultIP;
    return true;
  }

  std::string_view view(*fbsIP);

  if (!OpenShock::IPV4AddressFromStringView(ip, view)) {
    OS_LOGE(TAG, "failed to parse IP address");
    return false;
  }

  return true;
}

bool Config::Internal::Utils::FromJsonBool(bool& val, JSON::JsonView json, std::string_view name, bool defaultVal)
{
  JSON::JsonView jsonVal = json[name];
  if (!jsonVal.valid()) {
    val = defaultVal;
    return true;
  }

  if (!jsonVal.tryGetBool(val)) {
    OS_LOGE(TAG, "value at '%.*s' is not a bool", name.length(), name.data());
    return false;
  }

  return true;
}

bool Config::Internal::Utils::FromJsonU8(uint8_t& val, JSON::JsonView json, std::string_view name, uint8_t defaultVal)
{
  return utilFromJsonInt(val, json, name, defaultVal, 0, UINT8_MAX);
}

bool Config::Internal::Utils::FromJsonU16(uint16_t& val, JSON::JsonView json, std::string_view name, uint16_t defaultVal)
{
  return utilFromJsonInt(val, json, name, defaultVal, 0, UINT16_MAX);
}

bool Config::Internal::Utils::FromJsonI32(int32_t& val, JSON::JsonView json, std::string_view name, int32_t defaultVal)
{
  return utilFromJsonInt(val, json, name, defaultVal, INT32_MIN, INT32_MAX);
}

bool Config::Internal::Utils::FromJsonStr(std::string& str, JSON::JsonView json, std::string_view name)
{
  JSON::JsonView jsonVal = json[name];
  if (!jsonVal.valid()) {
    OS_LOGE(TAG, "value at '%.*s' is null", name.length(), name.data());
    return false;
  }

  std::string_view view;
  if (!jsonVal.tryGetStr(view)) {
    OS_LOGE(TAG, "value at '%.*s' is not a string", name.length(), name.data());
    return false;
  }

  str.assign(view);

  return true;
}

void Config::Internal::Utils::FromJsonStr(std::string& str, JSON::JsonView json, std::string_view name, const char* defaultStr)
{
  if (!FromJsonStr(str, json, name)) {
    str = defaultStr;
  }
}

bool Config::Internal::Utils::FromJsonIPAddress(IPAddress& ip, JSON::JsonView json, std::string_view name)
{
  JSON::JsonView jsonVal = json[name];
  if (!jsonVal.valid()) {
    OS_LOGE(TAG, "value at '%.*s' is null", name.length(), name.data());
    return false;
  }

  std::string_view view;
  if (!jsonVal.tryGetStr(view)) {
    OS_LOGE(TAG, "value at '%.*s' is not a string", name.length(), name.data());
    return false;
  }

  if (!OpenShock::IPV4AddressFromStringView(ip, view)) {
    OS_LOGE(TAG, "failed to parse IP address at '%.*s'", name.length(), name.data());
    return false;
  }

  return true;
}

void Config::Internal::Utils::FromJsonIPAddress(IPAddress& ip, JSON::JsonView json, std::string_view name, const IPAddress& defaultIP)
{
  if (!FromJsonIPAddress(ip, json, name)) {
    ip = defaultIP;
  }
}

bool Config::Internal::Utils::FromJsonGpioNum(gpio_num_t& val, JSON::JsonView json, std::string_view name)
{
  uint8_t u8Val;
  if (!FromJsonU8(u8Val, json, name, 0)) {
    return false;
  }

  return FromU8GpioNum(val, u8Val);
}

void Config::Internal::Utils::FromJsonGpioNum(gpio_num_t& val, JSON::JsonView json, std::string_view name, gpio_num_t defaultVal)
{
  if (!FromJsonGpioNum(val, json, name)) {
    val = defaultVal;
  }
}
