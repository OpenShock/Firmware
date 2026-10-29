#include "serialization/JsonAPI.h"

const char* const TAG = "JsonAPI";

#include "Logging.h"

#include <optional>
#include <string>
#include <utility>

// Log an invalid-response error together with the offending JSON, zero-copy
// straight out of the parse buffer (jsmn views are not NUL-terminated).
#define ESP_LOGJSONE(err, view) OS_LOGE(TAG, "Invalid JSON response (" err "): %.*s", static_cast<int>((view).raw().size()), (view).raw().data())

using namespace OpenShock::Serialization;

std::optional<JsonAPI::BackendVersionResponse> JsonAPI::ParseBackendVersionJsonResponse(int code, JSON::JsonView root)
{
  (void)code;

  if (!root.isObject()) {
    ESP_LOGJSONE("not an object", root);
    return std::nullopt;
  }

  JSON::JsonView data = root["data"];
  if (!data.isObject()) {
    ESP_LOGJSONE("value at 'data' is not an object", root);
    return std::nullopt;
  }

  auto version = data["version"].tryGetStr();
  if (!version) {
    ESP_LOGJSONE("value at 'data.version' is not a string", root);
    return std::nullopt;
  }

  auto commit = data["commit"].tryGetStr();
  if (!commit) {
    ESP_LOGJSONE("value at 'data.commit' is not a string", root);
    return std::nullopt;
  }

  auto currentTime = data["currentTime"].tryGetStr();
  if (!currentTime) {
    ESP_LOGJSONE("value at 'data.currentTime' is not a string", root);
    return std::nullopt;
  }

  return BackendVersionResponse {
    .version     = std::move(*version),
    .commit      = std::move(*commit),
    .currentTime = std::move(*currentTime),
  };
}

std::optional<JsonAPI::AccountLinkResponse> JsonAPI::ParseAccountLinkJsonResponse(int code, JSON::JsonView root)
{
  (void)code;

  if (!root.isObject()) {
    ESP_LOGJSONE("not an object", root);
    return std::nullopt;
  }

  auto data = root["data"].tryGetStr();
  if (!data) {
    ESP_LOGJSONE("value at 'data' is not a string", root);
    return std::nullopt;
  }

  return AccountLinkResponse {.authToken = std::move(*data)};
}
std::optional<JsonAPI::HubInfoResponse> JsonAPI::ParseHubInfoJsonResponse(int code, JSON::JsonView root)
{
  (void)code;

  if (!root.isObject()) {
    ESP_LOGJSONE("not an object", root);
    return std::nullopt;
  }

  JSON::JsonView data = root["data"];
  if (!data.isObject()) {
    ESP_LOGJSONE("value at 'data' is not an object", root);
    return std::nullopt;
  }

  auto hubId = data["id"].tryGetStr();
  if (!hubId) {
    ESP_LOGJSONE("value at 'data.id' is not a string", root);
    return std::nullopt;
  }

  auto hubName = data["name"].tryGetStr();
  if (!hubName) {
    ESP_LOGJSONE("value at 'data.name' is not a string", root);
    return std::nullopt;
  }

  JSON::JsonView hubShockers = data["shockers"];
  if (!hubShockers.isArray()) {
    ESP_LOGJSONE("value at 'data.shockers' is not an array", root);
    return std::nullopt;
  }

  if (hubId->empty() || hubName->empty()) {
    ESP_LOGJSONE("value at 'data.id' or 'data.name' is empty", root);
    return std::nullopt;
  }

  HubInfoResponse out {
    .hubId    = std::move(*hubId),
    .hubName  = std::move(*hubName),
    .shockers = {},
  };

  const int shockerCount = hubShockers.count();
  for (int i = 0; i < shockerCount; ++i) {
    JSON::JsonView shocker = hubShockers.at(i);

    auto shockerId = shocker["id"].tryGetStr();
    if (!shockerId) {
      ESP_LOGJSONE("value at 'shocker.id' is not a string", shocker);
      return std::nullopt;
    }
    if (shockerId->empty()) {
      ESP_LOGJSONE("value at 'shocker.id' is empty", shocker);
      return std::nullopt;
    }

    auto shockerRfId = shocker["rfId"].tryGetI64();
    if (!shockerRfId) {
      ESP_LOGJSONE("value at 'shocker.rfId' is not a number", shocker);
      return std::nullopt;
    }
    if (*shockerRfId < 0 || *shockerRfId > UINT16_MAX) {
      ESP_LOGJSONE("value at 'shocker.rfId' is not a valid uint16_t", shocker);
      return std::nullopt;
    }
    uint16_t shockerRfIdU16 = static_cast<uint16_t>(*shockerRfId);

    auto shockerModel = shocker["model"].tryGetStr();
    if (!shockerModel) {
      ESP_LOGJSONE("value at 'shocker.model' is not a string", shocker);
      return std::nullopt;
    }
    if (shockerModel->empty()) {
      ESP_LOGJSONE("value at 'shocker.model' is empty", shocker);
      return std::nullopt;
    }

    auto shockerModelType = OpenShock::TryParseShockerModelType(*shockerModel, true);  // PetTrainer is a typo in the API, we pass true to allow it
    if (!shockerModelType) {
      ESP_LOGJSONE("value at 'shocker.model' is not a valid shocker model", shocker);
      return std::nullopt;
    }

    out.shockers.push_back({.id = std::move(*shockerId), .rfId = shockerRfIdU16, .model = *shockerModelType});
  }

  return out;
}
std::optional<JsonAPI::AssignLcgResponse> JsonAPI::ParseAssignLcgJsonResponse(int code, JSON::JsonView root)
{
  (void)code;

  if (!root.isObject()) {
    ESP_LOGJSONE("not an object", root);
    return std::nullopt;
  }

  auto host    = root["host"].tryGetStr();
  auto path    = root["path"].tryGetStr();
  auto country = root["country"].tryGetStr();
  if (!host || !path || !country) {
    ESP_LOGJSONE("value at 'host', 'path' or 'country' is not a string", root);
    return std::nullopt;
  }

  auto port = root["port"].tryGetI64();
  if (!port) {
    ESP_LOGJSONE("value at 'port' is not a number", root);
    return std::nullopt;
  }
  if (*port < 0 || *port > UINT16_MAX) {
    ESP_LOGJSONE("value at 'port' is outside UINT16 bounds", root);
    return std::nullopt;
  }

  return AssignLcgResponse {
    .host    = std::move(*host),
    .port    = static_cast<uint16_t>(*port),
    .path    = std::move(*path),
    .country = std::move(*country),
  };
}
