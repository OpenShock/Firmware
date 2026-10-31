#include <freertos/FreeRTOS.h>

#include "BackendConfig.h"

const char* const TAG = "Config::BackendConfig";

#include "config/Nvs.h"
#include "config/Parse.h"
#include "Logging.h"

#include "json/Json.h"

using namespace OpenShock::Config;
namespace Fbs = OpenShock::Serialization::Configuration;

static constexpr const char* kNamespace = "backend";
static constexpr const char* kKeyDomain = "domain";
static constexpr const char* kKeyToken  = "token";

static constexpr const char* kNamespaces[] = {kNamespace};
static Section<BackendConfig> s_section({.name = "backend", .order = 3, .hubConfigField = Fbs::HubConfig::VT_BACKEND, .namespaces = kNamespaces});

Section<BackendConfig>& OpenShock::Config::BackendSection()
{
  return s_section;
}

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

flatbuffers::Offset<OpenShock::Serialization::Configuration::BackendConfig> BackendConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const
{
  auto domainOffset = builder.CreateString(domain);

  flatbuffers::Offset<flatbuffers::String> authTokenOffset;
  if (withSensitiveData) {
    authTokenOffset = builder.CreateString(authToken);
  } else {
    authTokenOffset = 0;
  }

  return Serialization::Configuration::CreateBackendConfig(builder, domainOffset, authTokenOffset);
}

bool BackendConfig::FromJSON(JSON::JsonView json)
{
  if (!json.valid()) {
    OS_LOGW(TAG, "Config is null, setting to default");
    *this = BackendConfig();
    return true;
  }

  if (!json.isObject()) {
    OS_LOGE(TAG, "json is not an object");
    return false;
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
  std::optional<Nvs::Handle> handle;
  switch (Nvs::Handle::OpenRead(partition, kNamespace, handle)) {
    case Nvs::Result::Error:
      return false;
    case Nvs::Result::Missing:
      *this = BackendConfig();
      return true;
    case Nvs::Result::Found:
      break;
  }
  return Nvs::Load(*handle, kKeyDomain, domain, CONFIG_OPENSHOCK_API_DOMAIN) && Nvs::Load(*handle, kKeyToken, authToken, "");
}

bool BackendConfig::SaveNvs(const char* partition, const BackendConfig* previous) const
{
  auto handle = Nvs::Handle::Open(partition, kNamespace, true);
  if (!handle) {
    return false;
  }
  return handle->SetStr(kKeyDomain, domain, previous != nullptr ? &previous->domain : nullptr) && handle->SetStr(kKeyToken, authToken, previous != nullptr ? &previous->authToken : nullptr) && handle->Commit();
}

std::optional<BackendConfig> OpenShock::Config::GetBackendConfig()
{
  return s_section.Get();
}

bool OpenShock::Config::SetBackendConfig(const BackendConfig& config)
{
  return s_section.Set(config);
}

std::optional<std::string> OpenShock::Config::GetBackendDomain()
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

std::optional<std::string> OpenShock::Config::GetBackendAuthToken()
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
