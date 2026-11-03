#include <freertos/FreeRTOS.h>

#include "BackendConfig.h"

const char* const TAG = "Config::BackendConfig";

#include "config/Nvs.h"
#include "config/Parse.h"
#include "Logging.h"

#include "json/Json.h"

using namespace OpenShock::Config;

static constexpr const char* kNamespace = "backend";
static constexpr const char* kKeyDomain = "domain";
static constexpr const char* kKeyToken  = "token";

static constexpr const char* kNamespaces[] = {kNamespace};
static Section<BackendConfig> s_section({.name = "backend", .order = 3, .namespaces = kNamespaces});

BackendConfig::BackendConfig()
  : domain(CONFIG_OPENSHOCK_API_DOMAIN)
  , authToken()
{
}

BackendConfig::BackendConfig(std::string_view domain, std::string_view authToken)
  : domain(domain)
  , authToken(authToken)
{
}

bool BackendConfig::FromJSON(JSON::JsonView json)
{
  if (auto done = Parse::ObjectOrDefault(*this, json, TAG)) {
    return *done;
  }

  domain = json["domain"].tryGetStr().value_or(CONFIG_OPENSHOCK_API_DOMAIN);

  authToken = json["authToken"].tryGetStr().value_or(std::string());

  return true;
}

void BackendConfig::ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const
{
  JSON::objBegin(gen, name);
  JSON::objSetString(gen, "domain", domain);

  if (withSensitiveData) {
    JSON::objSetString(gen, "authToken", authToken);
  }
  JSON::objEnd(gen, name);
}

bool BackendConfig::LoadNvs(const char* partition)
{
  auto handle = Nvs::Handle::OpenForLoad(partition, kNamespace);
  if (!handle) {
    return false;
  }
  return Nvs::Load(*handle, kKeyDomain, domain, CONFIG_OPENSHOCK_API_DOMAIN) && Nvs::Load(*handle, kKeyToken, authToken, "");
}

bool BackendConfig::SaveNvs(const char* partition, const BackendConfig* previous) const
{
  auto handle = Nvs::Handle::OpenForWrite(partition, kNamespace);
  if (!handle) {
    return false;
  }
  return Nvs::Saver(*handle, *this, previous).Set(kKeyDomain, &BackendConfig::domain).Set(kKeyToken, &BackendConfig::authToken).Commit();
}

BackendConfig OpenShock::Config::GetBackendConfig()
{
  return s_section.Get();
}

bool OpenShock::Config::SetBackendConfig(const BackendConfig& config)
{
  return s_section.Set(config);
}

std::string OpenShock::Config::GetBackendDomain()
{
  return s_section.Read([](const BackendConfig& backend) { return backend.domain; });
}

bool OpenShock::Config::SetBackendDomain(std::string domain)
{
  return s_section.Update([&](BackendConfig& backend) {
    backend.domain = std::move(domain);
    return true;
  });
}

bool OpenShock::Config::HasBackendAuthToken()
{
  return s_section.Read([](const BackendConfig& backend) { return !backend.authToken.empty(); });
}

std::string OpenShock::Config::GetBackendAuthToken()
{
  return s_section.Read([](const BackendConfig& backend) { return backend.authToken; });
}

bool OpenShock::Config::SetBackendAuthToken(std::string token)
{
  return s_section.Update([&](BackendConfig& backend) {
    backend.authToken = std::move(token);
    return true;
  });
}

bool OpenShock::Config::ClearBackendAuthToken()
{
  return s_section.Update([](BackendConfig& backend) {
    backend.authToken.clear();
    return true;
  });
}
