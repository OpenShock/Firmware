#include "config/internal/utils.h"

const char* const TAG = "Config::Internal::Utils";

#include "Logging.h"

using namespace OpenShock;

template<typename T>
bool _utilFromJsonInt(T& val, const cJSON* json, const char* name, T defaultVal, int minVal, int maxVal) {
  static_assert(std::is_integral<T>::value, "T must be an integral type");

  const cJSON* jsonVal = cJSON_GetObjectItemCaseSensitive(json, name);
  if (jsonVal == nullptr) {
    val = defaultVal;
    return true;
  }

  if (cJSON_IsNumber(jsonVal) == 0) {
    ESP_LOGE(TAG, "value at '%s' is not a number", name);
    return false;
  }

  int intVal = jsonVal->valueint;

  if (intVal < minVal) {
    ESP_LOGE(TAG, "value at '%s' is less than %d", name, minVal);
    return false;
  }

  if (intVal > maxVal) {
    ESP_LOGE(TAG, "value at '%s' is greater than %d", name, maxVal);
    return false;
  }

  val = static_cast<T>(intVal);

  return true;
}

void Config::Internal::Utils::FromFbsStr(std::string& str, const flatbuffers::String* fbsStr, const char* defaultStr) {
  if (fbsStr != nullptr) {
    str = fbsStr->c_str();
  } else {
    str = defaultStr;
  }
}

bool Config::Internal::Utils::FromJsonBool(bool& val, const cJSON* json, const char* name, bool defaultVal) {
  const cJSON* jsonVal = cJSON_GetObjectItemCaseSensitive(json, name);
  if (jsonVal == nullptr) {
    val = defaultVal;
    return true;
  }

  if (cJSON_IsBool(jsonVal) == 0) {
    ESP_LOGE(TAG, "value at '%s' is not a bool", name);
    return false;
  }

  val = cJSON_IsTrue(jsonVal);

  return true;
}

bool Config::Internal::Utils::FromJsonU8(uint8_t& val, const cJSON* json, const char* name, uint8_t defaultVal) {
  return _utilFromJsonInt(val, json, name, defaultVal, 0, UINT8_MAX);
}

bool Config::Internal::Utils::FromJsonU16(uint16_t& val, const cJSON* json, const char* name, uint16_t defaultVal) {
  return _utilFromJsonInt(val, json, name, defaultVal, 0, UINT16_MAX);
}

bool Config::Internal::Utils::FromJsonI32(int32_t& val, const cJSON* json, const char* name, int32_t defaultVal) {
  return _utilFromJsonInt(val, json, name, defaultVal, INT32_MIN, INT32_MAX);
}

bool Config::Internal::Utils::FromJsonStr(std::string& str, const cJSON* json, const char* name, const char* defaultStr) {
  const cJSON* jsonVal = cJSON_GetObjectItemCaseSensitive(json, name);
  if (jsonVal == nullptr) {
    str = defaultStr;
    return true;
  }

  if (cJSON_IsString(jsonVal) == 0) {
    ESP_LOGE(TAG, "value at '%s' is not a string", name);
    return false;
  }

  str = jsonVal->valuestring;

  return true;
}
