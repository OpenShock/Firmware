#include "serialization/JsonAPI.h"

#include "Logging.h"

const char* const TAG = "JsonAPI";

#define ESP_LOGJSONE(err, root) ESP_LOGE(TAG, "Invalid JSON response (" err "): %s", cJSON_PrintUnformatted(root))

using namespace OpenShock::Serialization;

bool JsonAPI::ParseLcgInstanceDetailsJsonResponse(int code, const cJSON* root, JsonAPI::LcgInstanceDetailsResponse& out) {
  (void)code;

  if (cJSON_IsObject(root) == 0) {
    ESP_LOGJSONE("not an object", root);
    return false;
  }

  out = {};

  const cJSON* name = cJSON_GetObjectItemCaseSensitive(root, "name");
  if (cJSON_IsString(name) == 0) {
    ESP_LOGJSONE("value at 'data.name' is not a string", root);
    return false;
  }

  const cJSON* version = cJSON_GetObjectItemCaseSensitive(root, "version");
  if (cJSON_IsString(version) == 0) {
    ESP_LOGJSONE("value at 'data.version' is not a string", root);
    return false;
  }

  const cJSON* currentTime = cJSON_GetObjectItemCaseSensitive(root, "currentTime");
  if (cJSON_IsString(currentTime) == 0) {
    ESP_LOGJSONE("value at 'data.currentTime' is not a string", root);
    return false;
  }

  const cJSON* countryCode = cJSON_GetObjectItemCaseSensitive(root, "countryCode");
  if (cJSON_IsString(countryCode) == 0) {
    ESP_LOGJSONE("value at 'data.countryCode' is not a string", root);
    return false;
  }

  const cJSON* fqdn = cJSON_GetObjectItemCaseSensitive(root, "fqdn");
  if (cJSON_IsString(fqdn) == 0) {
    ESP_LOGJSONE("value at 'data.fqdn' is not a string", root);
    return false;
  }

  out.name        = name->valuestring;
  out.version     = version->valuestring;
  out.currentTime = currentTime->valuestring;
  out.countryCode = countryCode->valuestring;
  out.fqdn        = fqdn->valuestring;

  return true;
}
bool JsonAPI::ParseBackendVersionJsonResponse(int code, const cJSON* root, JsonAPI::BackendVersionResponse& out) {
  (void)code;

  if (cJSON_IsObject(root) == 0) {
    ESP_LOGJSONE("not an object", root);
    return false;
  }

  const cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
  if (cJSON_IsObject(data) == 0) {
    ESP_LOGJSONE("value at 'data' is not an object", root);
    return false;
  }

  out = {};

  const cJSON* version = cJSON_GetObjectItemCaseSensitive(data, "version");
  if (cJSON_IsString(version) == 0) {
    ESP_LOGJSONE("value at 'data.version' is not a string", root);
    return false;
  }

  const cJSON* commit = cJSON_GetObjectItemCaseSensitive(data, "commit");
  if (cJSON_IsString(commit) == 0) {
    ESP_LOGJSONE("value at 'data.commit' is not a string", root);
    return false;
  }

  const cJSON* currentTime = cJSON_GetObjectItemCaseSensitive(data, "currentTime");
  if (cJSON_IsString(currentTime) == 0) {
    ESP_LOGJSONE("value at 'data.currentTime' is not a string", root);
    return false;
  }

  out.version     = version->valuestring;
  out.commit      = commit->valuestring;
  out.currentTime = currentTime->valuestring;

  return true;
}

bool JsonAPI::ParseAccountLinkJsonResponse(int code, const cJSON* root, JsonAPI::AccountLinkResponse& out) {
  (void)code;

  if (cJSON_IsObject(root) == 0) {
    ESP_LOGJSONE("not an object", root);
    return false;
  }

  const cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
  if (cJSON_IsString(data) == 0) {
    ESP_LOGJSONE("value at 'data' is not a string", root);
    return false;
  }

  out = {};

  out.authToken = data->valuestring;

  return true;
}
bool JsonAPI::ParseDeviceInfoJsonResponse(int code, const cJSON* root, JsonAPI::DeviceInfoResponse& out) {
  (void)code;
  
  if (cJSON_IsObject(root) == 0) {
    ESP_LOGJSONE("not an object", root);
    return false;
  }

  const cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
  if (cJSON_IsObject(data) == 0) {
    ESP_LOGJSONE("value at 'data' is not an object", root);
    return false;
  }

  const cJSON* deviceId = cJSON_GetObjectItemCaseSensitive(data, "id");
  if (cJSON_IsString(deviceId) == 0) {
    ESP_LOGJSONE("value at 'data.id' is not a string", root);
    return false;
  }

  const cJSON* deviceName = cJSON_GetObjectItemCaseSensitive(data, "name");
  if (cJSON_IsString(deviceName) == 0) {
    ESP_LOGJSONE("value at 'data.name' is not a string", root);
    return false;
  }

  const cJSON* deviceShockers = cJSON_GetObjectItemCaseSensitive(data, "shockers");
  if (cJSON_IsArray(deviceShockers) == 0) {
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
    if (cJSON_IsString(shockerId) == 0) {
      ESP_LOGJSONE("value at 'shocker.id' is not a string", shocker);
      return false;
    }
    const char* shockerIdStr = shockerId->valuestring;

    if (shockerIdStr == nullptr || shockerIdStr[0] == '\0') {
      ESP_LOGJSONE("value at 'shocker.id' is empty", shocker);
      return false;
    }

    const cJSON* shockerRfId = cJSON_GetObjectItemCaseSensitive(shocker, "rfId");
    if (cJSON_IsNumber(shockerRfId) == 0) {
      ESP_LOGJSONE("value at 'shocker.rfId' is not a number", shocker);
      return false;
    }
    int shockerRfIdInt = shockerRfId->valueint;
    if (shockerRfIdInt < 0 || shockerRfIdInt > UINT16_MAX) {
      ESP_LOGJSONE("value at 'shocker.rfId' is not a valid uint16_t", shocker);
      return false;
    }
    uint16_t shockerRfIdU16 = static_cast<uint16_t>(shockerRfIdInt);

    const cJSON* shockerModel = cJSON_GetObjectItemCaseSensitive(shocker, "model");
    if (cJSON_IsString(shockerModel) == 0) {
      ESP_LOGJSONE("value at 'shocker.model' is not a string", shocker);
      return false;
    }
    const char* shockerModelStr = shockerModel->valuestring;

    if (shockerModelStr == nullptr || shockerModelStr[0] == '\0') {
      ESP_LOGJSONE("value at 'shocker.model' is empty", shocker);
      return false;
    }

    OpenShock::ShockerModelType shockerModelType;
    if (!OpenShock::ShockerModelTypeFromString(shockerModelStr, shockerModelType, true)) { // PetTrainer is a typo in the API, we pass true to allow it
      ESP_LOGJSONE("value at 'shocker.model' is not a valid shocker model", shocker);
      return false;
    }

    out.shockers.push_back({.id = shockerIdStr, .rfId = shockerRfIdU16, .model = shockerModelType});
  }

  return true;
}
bool JsonAPI::ParseAssignLcgJsonResponse(int code, const cJSON* root, JsonAPI::AssignLcgResponse& out) {
  (void)code;

  if (cJSON_IsObject(root) == 0) {
    ESP_LOGJSONE("not an object", root);
    return false;
  }

  const cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
  if (cJSON_IsObject(data) == 0) {
    ESP_LOGJSONE("value at 'data' is not an object", root);
    return false;
  }

  const cJSON* fqdn    = cJSON_GetObjectItemCaseSensitive(data, "fqdn");
  const cJSON* country = cJSON_GetObjectItemCaseSensitive(data, "country");

  if (cJSON_IsString(fqdn) == 0 || cJSON_IsString(country) == 0) {
    ESP_LOGJSONE("value at 'data.fqdn' or 'data.country' is not a string", root);
    return false;
  }

  out = {};

  out.fqdn    = fqdn->valuestring;
  out.country = country->valuestring;

  return true;
}
