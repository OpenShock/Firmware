#include "serialization/JsonAPI.h"

const char* const TAG = "JsonAPI";

#include "Logging.h"

#include <string>

// Log an invalid-response error together with the offending JSON, zero-copy
// straight out of the parse buffer (jsmn views are not NUL-terminated).
#define ESP_LOGJSONE(err, view) OS_LOGE(TAG, "Invalid JSON response (" err "): %.*s", static_cast<int>((view).raw().size()), (view).raw().data())

using namespace OpenShock::Serialization;

bool JsonAPI::ParseBackendVersionJsonResponse(int code, JSON::JsonView root, JsonAPI::BackendVersionResponse& out)
{
  (void)code;

  if (!root.isObject()) {
    ESP_LOGJSONE("not an object", root);
    return false;
  }

  JSON::JsonView data = root["data"];
  if (!data.isObject()) {
    ESP_LOGJSONE("value at 'data' is not an object", root);
    return false;
  }

  out = {};

  std::string version;
  if (!data["version"].tryGetStr(version)) {
    ESP_LOGJSONE("value at 'data.version' is not a string", root);
    return false;
  }

  std::string commit;
  if (!data["commit"].tryGetStr(commit)) {
    ESP_LOGJSONE("value at 'data.commit' is not a string", root);
    return false;
  }

  std::string currentTime;
  if (!data["currentTime"].tryGetStr(currentTime)) {
    ESP_LOGJSONE("value at 'data.currentTime' is not a string", root);
    return false;
  }

  out.version.assign(version);
  out.commit.assign(commit);
  out.currentTime.assign(currentTime);

  return true;
}

bool JsonAPI::ParseAccountLinkJsonResponse(int code, JSON::JsonView root, JsonAPI::AccountLinkResponse& out)
{
  (void)code;

  if (!root.isObject()) {
    ESP_LOGJSONE("not an object", root);
    return false;
  }

  std::string data;
  if (!root["data"].tryGetStr(data)) {
    ESP_LOGJSONE("value at 'data' is not a string", root);
    return false;
  }

  out = {};

  out.authToken.assign(data);

  return true;
}
bool JsonAPI::ParseHubInfoJsonResponse(int code, JSON::JsonView root, JsonAPI::HubInfoResponse& out)
{
  (void)code;

  if (!root.isObject()) {
    ESP_LOGJSONE("not an object", root);
    return false;
  }

  JSON::JsonView data = root["data"];
  if (!data.isObject()) {
    ESP_LOGJSONE("value at 'data' is not an object", root);
    return false;
  }

  std::string hubId;
  if (!data["id"].tryGetStr(hubId)) {
    ESP_LOGJSONE("value at 'data.id' is not a string", root);
    return false;
  }

  std::string hubName;
  if (!data["name"].tryGetStr(hubName)) {
    ESP_LOGJSONE("value at 'data.name' is not a string", root);
    return false;
  }

  JSON::JsonView hubShockers = data["shockers"];
  if (!hubShockers.isArray()) {
    ESP_LOGJSONE("value at 'data.shockers' is not an array", root);
    return false;
  }

  out = {};

  out.hubId.assign(hubId);
  out.hubName.assign(hubName);

  if (out.hubId.empty() || out.hubName.empty()) {
    ESP_LOGJSONE("value at 'data.id' or 'data.name' is empty", root);
    return false;
  }

  const int shockerCount = hubShockers.count();
  for (int i = 0; i < shockerCount; ++i) {
    JSON::JsonView shocker = hubShockers.at(i);

    std::string shockerId;
    if (!shocker["id"].tryGetStr(shockerId)) {
      ESP_LOGJSONE("value at 'shocker.id' is not a string", shocker);
      return false;
    }
    if (shockerId.empty()) {
      ESP_LOGJSONE("value at 'shocker.id' is empty", shocker);
      return false;
    }

    int64_t shockerRfId;
    if (!shocker["rfId"].tryGetI64(shockerRfId)) {
      ESP_LOGJSONE("value at 'shocker.rfId' is not a number", shocker);
      return false;
    }
    if (shockerRfId < 0 || shockerRfId > UINT16_MAX) {
      ESP_LOGJSONE("value at 'shocker.rfId' is not a valid uint16_t", shocker);
      return false;
    }
    uint16_t shockerRfIdU16 = static_cast<uint16_t>(shockerRfId);

    std::string shockerModel;
    if (!shocker["model"].tryGetStr(shockerModel)) {
      ESP_LOGJSONE("value at 'shocker.model' is not a string", shocker);
      return false;
    }
    if (shockerModel.empty()) {
      ESP_LOGJSONE("value at 'shocker.model' is empty", shocker);
      return false;
    }

    OpenShock::ShockerModelType shockerModelType;
    if (!OpenShock::TryParseShockerModelType(shockerModelType, shockerModel, true)) {  // PetTrainer is a typo in the API, we pass true to allow it
      ESP_LOGJSONE("value at 'shocker.model' is not a valid shocker model", shocker);
      return false;
    }

    out.shockers.push_back({.id = std::string(shockerId), .rfId = shockerRfIdU16, .model = shockerModelType});
  }

  return true;
}
bool JsonAPI::ParseAssignLcgJsonResponse(int code, JSON::JsonView root, JsonAPI::AssignLcgResponse& out)
{
  (void)code;

  if (!root.isObject()) {
    ESP_LOGJSONE("not an object", root);
    return false;
  }

  std::string host;
  std::string path;
  std::string country;
  if (!root["host"].tryGetStr(host) || !root["path"].tryGetStr(path) || !root["country"].tryGetStr(country)) {
    ESP_LOGJSONE("value at 'host', 'path' or 'country' is not a string", root);
    return false;
  }

  int64_t port;
  if (!root["port"].tryGetI64(port)) {
    ESP_LOGJSONE("value at 'port' is not a number", root);
    return false;
  }
  if (port < 0 || port > UINT16_MAX) {
    ESP_LOGJSONE("value at 'port' is outside UINT16 bounds", root);
    return false;
  }

  out = {};

  out.host.assign(host);
  out.port = static_cast<uint16_t>(port);
  out.path.assign(path);
  out.country.assign(country);

  return true;
}
