#include "config/internal/utils.h"

const char* const TAG = "Config::Internal::Utils";

#include "Chipset.h"
#include "Logging.h"
#include "util/IPAddressUtils.h"

using namespace OpenShock;

template<typename T>
bool _utilFromJsonInt(T& val, const cJSON* json, const char* name, T defaultVal, int minVal, int maxVal)
{
  static_assert(std::is_integral<T>::value, "T must be an integral type");

  const cJSON* jsonVal = cJSON_GetObjectItemCaseSensitive(json, name);
  if (jsonVal == nullptr) {
    return false;
  }

  if (cJSON_IsNumber(jsonVal) == 0) {
    OS_LOGE(TAG, "value at '%s' is not a number", name);
    return false;
  }

  int intVal = jsonVal->valueint;

  if (intVal < minVal) {
    OS_LOGE(TAG, "value at '%s' is less than %d", name, minVal);
    return false;
  }

  if (intVal > maxVal) {
    OS_LOGE(TAG, "value at '%s' is greater than %d", name, maxVal);
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

bool Config::Internal::Utils::FromJsonBool(bool& val, const cJSON* json, const char* name, bool defaultVal)
{
  const cJSON* jsonVal = cJSON_GetObjectItemCaseSensitive(json, name);
  if (jsonVal == nullptr) {
    val = defaultVal;
    return true;
  }

  if (cJSON_IsBool(jsonVal) == 0) {
    OS_LOGE(TAG, "value at '%s' is not a bool", name);
    return false;
  }

  val = cJSON_IsTrue(jsonVal);

  return true;
}

bool Config::Internal::Utils::FromJsonU8(uint8_t& val, const cJSON* json, const char* name, uint8_t defaultVal)
{
  return _utilFromJsonInt(val, json, name, defaultVal, 0, UINT8_MAX);
}

bool Config::Internal::Utils::FromJsonU16(uint16_t& val, const cJSON* json, const char* name, uint16_t defaultVal)
{
  return _utilFromJsonInt(val, json, name, defaultVal, 0, UINT16_MAX);
}

bool Config::Internal::Utils::FromJsonI32(int32_t& val, const cJSON* json, const char* name, int32_t defaultVal)
{
  return _utilFromJsonInt(val, json, name, defaultVal, INT32_MIN, INT32_MAX);
}

bool Config::Internal::Utils::FromJsonStr(std::string& str, const cJSON* json, const char* name)
{
  const cJSON* jsonVal = cJSON_GetObjectItemCaseSensitive(json, name);
  if (jsonVal == nullptr) {
    OS_LOGE(TAG, "value at '%s' is null", name);
    return false;
  }

  if (cJSON_IsString(jsonVal) == 0) {
    OS_LOGE(TAG, "value at '%s' is not a string", name);
    return false;
  }

  str = jsonVal->valuestring;

  return true;
}

void Config::Internal::Utils::FromJsonStr(std::string& str, const cJSON* json, const char* name, const char* defaultStr)
{
  if (!FromJsonStr(str, json, name)) {
    str = defaultStr;
  }
}

bool Config::Internal::Utils::FromJsonIPAddress(IPAddress& ip, const cJSON* json, const char* name)
{
  const cJSON* jsonVal = cJSON_GetObjectItemCaseSensitive(json, name);
  if (jsonVal == nullptr) {
    OS_LOGE(TAG, "value at '%s' is null", name);
    return false;
  }

  if (cJSON_IsString(jsonVal) == 0) {
    OS_LOGE(TAG, "value at '%s' is not a string", name);
    return false;
  }

  std::string_view view(jsonVal->valuestring);

  if (!OpenShock::IPV4AddressFromStringView(ip, view)) {
    OS_LOGE(TAG, "failed to parse IP address at '%s'", name);
    return false;
  }

  return true;
}

void Config::Internal::Utils::FromJsonIPAddress(IPAddress& ip, const cJSON* json, const char* name, const IPAddress& defaultIP)
{
  if (!FromJsonIPAddress(ip, json, name)) {
    ip = defaultIP;
  }
}

bool Config::Internal::Utils::FromJsonGpioNum(gpio_num_t& val, const cJSON* json, const char* name)
{
  uint8_t u8Val;
  if (!FromJsonU8(u8Val, json, name, 0)) {
    return false;
  }

  return FromU8GpioNum(val, u8Val);
}

void Config::Internal::Utils::FromJsonGpioNum(gpio_num_t& val, const cJSON* json, const char* name, gpio_num_t defaultVal)
{
  if (!FromJsonGpioNum(val, json, name)) {
    val = defaultVal;
  }
}
