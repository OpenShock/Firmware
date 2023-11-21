#include "serialization/JsonAPI.h"

#include "Logging.h"

const char* const TAG = "JsonAPI";

#define ESP_LOGJSONE(err, root) ESP_LOGE(TAG, "Invalid JSON response (" err "): %s", cJSON_PrintUnformatted(root))

using namespace OpenShock::Serialization;

bool JsonAPI::ParseAccountLinkJsonResponse(int code, const cJSON* root, JsonAPI::AccountLinkResponse& out) {
  if (!cJSON_IsObject(root)) {
    ESP_LOGJSONE("not an object", root);
    return false;
  }

  const cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
  if (!cJSON_IsString(data)) {
    ESP_LOGJSONE("value at 'data' is not a string", root);
    return false;
  }

  out = {};

  out.authToken = data->valuestring;

  return true;
}
bool JsonAPI::ParseDeviceInfoJsonResponse(int code, const cJSON* root, JsonAPI::DeviceInfoResponse& out) {
  if (!cJSON_IsObject(root)) {
    ESP_LOGJSONE("not an object", root);
    return false;
  }

  const cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
  if (!cJSON_IsObject(data)) {
    ESP_LOGJSONE("value at 'data' is not an object", root);
    return false;
  }

  const cJSON* deviceId = cJSON_GetObjectItemCaseSensitive(data, "id");
  if (!cJSON_IsString(deviceId)) {
    ESP_LOGJSONE("value at 'data.id' is not a string", root);
    return false;
  }

  const cJSON* deviceName = cJSON_GetObjectItemCaseSensitive(data, "name");
  if (!cJSON_IsString(deviceName)) {
    ESP_LOGJSONE("value at 'data.name' is not a string", root);
    return false;
  }

  const cJSON* deviceShockers = cJSON_GetObjectItemCaseSensitive(data, "shockers");
  if (!cJSON_IsArray(deviceShockers)) {
    ESP_LOGJSONE("value at 'data.shockers' is not an array", root);
    return false;
  }

  out = {};

  out.deviceId   = deviceId->valuestring;
  out.deviceName = deviceName->valuestring;

  if (out.deviceId.empty() || out.deviceName.empty()) {
    ESP_LOGJSONE("value at 'data.id' or 'data.name' is empty", root);
    return false;
  }

  cJSON* shocker = nullptr;
  cJSON_ArrayForEach(shocker, deviceShockers) {
    const cJSON* shockerId = cJSON_GetObjectItemCaseSensitive(shocker, "id");
    if (!cJSON_IsString(shockerId)) {
      ESP_LOGJSONE("value at 'shocker.id' is not a string", shocker);
      return false;
    }
    const char* shockerIdStr = shockerId->valuestring;

    if (shockerIdStr == nullptr || shockerIdStr[0] == '\0') {
      ESP_LOGJSONE("value at 'shocker.id' is empty", shocker);
      return false;
    }

    const cJSON* shockerRfId = cJSON_GetObjectItemCaseSensitive(shocker, "rfId");
    if (!cJSON_IsNumber(shockerRfId)) {
      ESP_LOGJSONE("value at 'shocker.rfId' is not a number", shocker);
      return false;
    }
    int shockerRfIdInt = shockerRfId->valueint;
    if (shockerRfIdInt < 0 || shockerRfIdInt > UINT16_MAX) {
      ESP_LOGJSONE("value at 'shocker.rfId' is not a valid uint16_t", shocker);
      return false;
    }
    std::uint16_t shockerRfIdU16 = (std::uint16_t)shockerRfIdInt;

    const cJSON* shockerModel = cJSON_GetObjectItemCaseSensitive(shocker, "model");
    if (!cJSON_IsString(shockerModel)) {
      ESP_LOGJSONE("value at 'shocker.model' is not a string", shocker);
      return false;
    }
    const char* shockerModelStr = shockerModel->valuestring;

    if (shockerModelStr == nullptr || shockerModelStr[0] == '\0') {
      ESP_LOGJSONE("value at 'shocker.model' is empty", shocker);
      return false;
    }

    OpenShock::ShockerModelType shockerModelType;
    if (strcmp(shockerModelStr, "CaiXianlin") == 0 || strcmp(shockerModelStr, "CaiXianLin") == 0 || strcmp(shockerModelStr, "XLC") == 0 || strcmp(shockerModelStr, "CXL") == 0) {
      shockerModelType = OpenShock::ShockerModelType::CaiXianlin;
    } else if (strcmp(shockerModelStr, "PetTrainer") == 0 || strcmp(shockerModelStr, "PT") == 0) {
      shockerModelType = OpenShock::ShockerModelType::PetTrainer;
    } else {
      ESP_LOGJSONE("value at 'shocker.model' is not a valid shocker model", shocker);
      return false;
    }

    out.shockers.push_back({.id = shockerIdStr, .rfId = shockerRfIdU16, .model = shockerModelType});
  }

  return true;
}
bool JsonAPI::ParseAssignLcgJsonResponse(int code, const cJSON* root, JsonAPI::AssignLcgResponse& out) {
  if (!cJSON_IsObject(root)) {
    ESP_LOGJSONE("not an object", root);
    return false;
  }

  const cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
  if (!cJSON_IsObject(data)) {
    ESP_LOGJSONE("value at 'data' is not an object", root);
    return false;
  }

  const cJSON* fqdn    = cJSON_GetObjectItemCaseSensitive(data, "fqdn");
  const cJSON* country = cJSON_GetObjectItemCaseSensitive(data, "country");

  if (!cJSON_IsString(fqdn) || !cJSON_IsString(country)) {
    ESP_LOGJSONE("value at 'data.fqdn' or 'data.country' is not a string", root);
    return false;
  }

  out = {};

  out.fqdn    = fqdn->valuestring;
  out.country = country->valuestring;

  return true;
}
