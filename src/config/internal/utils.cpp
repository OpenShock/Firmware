#include "config/internal/utils.h"

#include "Logging.h"

const char* const TAG = "Config::Internal::Utils";

using namespace OpenShock;

void Config::Internal::Utils::FromFbsStr(std::string& str, const flatbuffers::String* fbsStr, const char* defaultStr) {
  if (fbsStr != nullptr) {
    str = fbsStr->c_str();
  } else {
    str = defaultStr;
  }
}

void Config::Internal::Utils::FromJsonBool(bool& val, const cJSON* json, const char* name, bool defaultVal) {
  const cJSON* jsonVal = cJSON_GetObjectItemCaseSensitive(json, name);
  if (jsonVal != nullptr) {
    if (cJSON_IsBool(jsonVal) == 0) {
      ESP_LOGE(TAG, "value at '%s' is not a bool", name);
      return;
    }
    val = cJSON_IsTrue(jsonVal);
  } else {
    val = defaultVal;
  }
}

void Config::Internal::Utils::FromJsonU8(std::uint8_t& val, const cJSON* json, const char* name, std::uint8_t defaultVal) {
  const cJSON* jsonVal = cJSON_GetObjectItemCaseSensitive(json, name);
  if (jsonVal == nullptr) {
    val = defaultVal;
    return;
  }

  if (cJSON_IsNumber(jsonVal) == 0) {
    ESP_LOGE(TAG, "value at '%s' is not a number", name);
    return;
  }

  int intVal = jsonVal->valueint;

  if (intVal < 0) {
    ESP_LOGE(TAG, "value at '%s' is negative", name);
    return;
  }

  if (intVal > UINT8_MAX) {
    ESP_LOGE(TAG, "value at '%s' is greater than UINT8_MAX", name);
    return;
  }

  val = static_cast<std::uint8_t>(intVal);
}

void Config::Internal::Utils::FromJsonU16(std::uint16_t& val, const cJSON* json, const char* name, std::uint16_t defaultVal) {
  const cJSON* jsonVal = cJSON_GetObjectItemCaseSensitive(json, name);
  if (jsonVal == nullptr) {
    val = defaultVal;
    return;
  }

  if (cJSON_IsNumber(jsonVal) == 0) {
    ESP_LOGE(TAG, "value at '%s' is not a number", name);
    return;
  }

  int intVal = jsonVal->valueint;

  if (intVal < 0) {
    ESP_LOGE(TAG, "value at '%s' is negative", name);
    return;
  }

  if (intVal > UINT16_MAX) {
    ESP_LOGE(TAG, "value at '%s' is greater than UINT16_MAX", name);
    return;
  }

  val = static_cast<std::uint16_t>(intVal);
}

void Config::Internal::Utils::FromJsonStr(std::string& str, const cJSON* json, const char* name, const char* defaultStr) {
  const cJSON* jsonVal = cJSON_GetObjectItemCaseSensitive(json, name);
  if (jsonVal != nullptr) {
    if (cJSON_IsString(jsonVal) == 0) {
      ESP_LOGE(TAG, "value at '%s' is not a string", name);
      return;
    }
    str = jsonVal->valuestring;
  } else {
    str = defaultStr;
  }
}
