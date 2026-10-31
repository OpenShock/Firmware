#include <freertos/FreeRTOS.h>

#include "wifi/WiFiConfig.h"

const char* const TAG = "Config::WiFiConfig";

#include "config/Nvs.h"
#include "config/Parse.h"
#include "Convert.h"
#include "Logging.h"
#include "util/HostnameUtils.h"

#include "json/Json.h"

#include <algorithm>
#include <bitset>
#include <cstdio>
#include <cstring>

using namespace OpenShock::Config;
namespace Fbs = OpenShock::Serialization::Configuration;

static_assert(OpenShock::HostnameUtils::IsValid(CONFIG_OPENSHOCK_FW_HOSTNAME), "CONFIG_OPENSHOCK_FW_HOSTNAME must be 1-32 letters, digits or hyphens, not starting or ending with a hyphen");

static constexpr const char* kNamespaceCfg   = "wifi.cfg";
static constexpr const char* kNamespaceCreds = "wifi";
static constexpr const char* kKeyApSsid      = "ap_ssid";
static constexpr const char* kKeyHostname    = "hostname";

static constexpr const char* kNamespaces[] = {kNamespaceCfg, kNamespaceCreds};
static Section<WiFiConfig> s_section({.name = "wifi", .order = 1, .hubConfigField = Fbs::HubConfig::VT_WIFI, .namespaces = kNamespaces});

WiFiConfig::WiFiConfig()
  : accessPointSSID(CONFIG_OPENSHOCK_FW_AP_PREFIX)
  , hostname(CONFIG_OPENSHOCK_FW_HOSTNAME)
  , credentialsList()
{
}

WiFiConfig::WiFiConfig(std::string_view accessPointSSID, std::string_view hostname, const std::vector<WiFiCredentials>& credentialsList)
  : accessPointSSID(accessPointSSID)
  , hostname(hostname)
  , credentialsList(credentialsList)
{
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::WiFiConfig> WiFiConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const
{
  std::vector<flatbuffers::Offset<OpenShock::Serialization::Configuration::WiFiCredentials>> fbsCredentialsList;
  fbsCredentialsList.reserve(credentialsList.size());

  for (auto& credentials : credentialsList) {
    fbsCredentialsList.push_back(credentials.ToFlatbuffers(builder, withSensitiveData));
  }

  return Serialization::Configuration::CreateWiFiConfig(builder, builder.CreateString(accessPointSSID), builder.CreateString(hostname), builder.CreateVector(fbsCredentialsList));
}

bool WiFiConfig::FromJSON(JSON::JsonView json)
{
  if (auto done = Parse::ObjectOrDefault(*this, json, TAG)) {
    return *done;
  }

  accessPointSSID = json["accessPointSSID"].tryGetStr().value_or(CONFIG_OPENSHOCK_FW_AP_PREFIX);

  hostname = json["hostname"].tryGetStr().value_or(CONFIG_OPENSHOCK_FW_HOSTNAME);

  JSON::JsonView credentialsListJson = json["credentials"];
  if (!credentialsListJson.valid()) {
    OS_LOGE(TAG, "credentials is null");
    return false;
  }

  if (!credentialsListJson.isArray()) {
    OS_LOGE(TAG, "credentials is not an array");
    return false;
  }

  Parse::JsonArray(credentialsList, credentialsListJson);

  return true;
}

void WiFiConfig::ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const
{
  JSON::objBegin(gen, name);
  JSON::objSetString(gen, "accessPointSSID", accessPointSSID);
  JSON::objSetString(gen, "hostname", hostname);

  json_gen_push_array(gen, "credentials");
  for (auto& credentials : credentialsList) {
    credentials.ToJSON(gen, nullptr, withSensitiveData);
  }
  json_gen_pop_array(gen);
  JSON::objEnd(gen, name);
}

namespace {
  // The NVS key of a credential: its id in decimal ("1".."255")
  void credentialKey(uint8_t id, char (&key)[4])
  {
    snprintf(key, sizeof(key), "%u", id);
  }

  // The one credential with `id` / `ssid`, const or mutable with the list; null when there is none
  template<typename List>
  auto findById(List& list, uint8_t id) -> decltype(list.data())
  {
    for (auto& creds : list) {
      if (creds.id == id) return &creds;
    }
    return nullptr;
  }

  template<typename List>
  auto findBySsid(List& list, std::string_view ssid) -> decltype(list.data())
  {
    for (auto& creds : list) {
      if (creds.ssid == ssid) return &creds;
    }
    return nullptr;
  }

  // The lowest id in 1..255 no credential in `list` holds; 0 when every id is taken
  uint8_t lowestFreeId(const std::vector<WiFiCredentials>& list)
  {
    std::bitset<256> used;
    for (const auto& creds : list) {
      used[creds.id] = true;
    }
    for (std::size_t id = 1; id < used.size(); ++id) {
      if (!used[id]) return static_cast<uint8_t>(id);
    }
    return 0;
  }
}  // namespace

void WiFiConfig::Normalize()
{
  if (!HostnameUtils::IsValid(hostname)) {
    OS_LOGW(TAG, "Hostname \"%s\" is not valid, using the default", hostname.c_str());
    hostname = CONFIG_OPENSHOCK_FW_HOSTNAME;
  }

  // Each credential is stored under its id, so 0 (what a missing id parses to, and what a new network comes in with)
  // and a repeated id would be lost on flash. Those get the lowest free id instead; one that finds none is dropped.
  std::bitset<256> seenIds;
  for (auto it = credentialsList.begin(); it != credentialsList.end();) {
    if (it->id != 0 && !seenIds[it->id]) {
      seenIds[it->id] = true;
      ++it;
      continue;
    }
    const uint8_t freeId = lowestFreeId(credentialsList);
    if (freeId == 0) {
      OS_LOGW(TAG, "Dropping credential %u: no free id left", it->id);
      it = credentialsList.erase(it);
      continue;
    }
    OS_LOGW(TAG, "Credential %u renumbered to %u", it->id, freeId);
    it->id          = freeId;
    seenIds[freeId] = true;
    ++it;
  }
}

bool WiFiConfig::LoadNvs(const char* partition)
{
  auto handle = Nvs::Handle::OpenForLoad(partition, kNamespaceCfg);
  if (!handle || !Nvs::Load(*handle, kKeyApSsid, accessPointSSID, CONFIG_OPENSHOCK_FW_AP_PREFIX) || !Nvs::Load(*handle, kKeyHostname, hostname, CONFIG_OPENSHOCK_FW_HOSTNAME)) {
    return false;
  }

  credentialsList.clear();
  handle = Nvs::Handle::OpenForLoad(partition, kNamespaceCreds);
  if (!handle) {
    return false;
  }

  // Enumerated, never counted: the keys are the ids. Collected first, since reading while iterating is not allowed.
  std::vector<std::string> keys;
  if (!handle->ForEachKey(NVS_TYPE_BLOB, [&](const char* key) { keys.emplace_back(key); })) {
    OS_LOGE(TAG, "Failed to enumerate credentials");
    return false;
  }
  for (const auto& key : keys) {
    auto id = Convert::ToUint8(key);
    if (!id || *id == 0) {
      OS_LOGW(TAG, "Ignoring credential under key %s", key.c_str());
      continue;
    }
    // One credential that does not read is dropped, not the section: a failed load runs the section on an empty list,
    // and its next save then sweeps every stored credential that is not in that list
    std::vector<uint8_t> blob;
    WiFiCredentials creds;
    if (handle->GetBlob(key.c_str(), blob) != Nvs::Result::Found || !creds.FromBlob(blob) || creds.id != *id) {
      OS_LOGW(TAG, "Dropping unreadable credential %s", key.c_str());
      continue;
    }
    credentialsList.push_back(std::move(creds));
  }
  std::sort(credentialsList.begin(), credentialsList.end(), [](const WiFiCredentials& a, const WiFiCredentials& b) { return a.id < b.id; });
  return true;
}

bool WiFiConfig::SaveNvs(const char* partition, const WiFiConfig* previous) const
{
  {
    auto handle = Nvs::Handle::Open(partition, kNamespaceCfg, true);
    if (!handle) {
      return false;
    }
    if (!Nvs::Saver(*handle, *this, previous).Set(kKeyApSsid, &WiFiConfig::accessPointSSID).Set(kKeyHostname, &WiFiConfig::hostname).Commit()) {
      return false;
    }
  }

  auto handle = Nvs::Handle::Open(partition, kNamespaceCreds, true);
  if (!handle) {
    return false;
  }

  // Credentials that are gone: the previous list's, or - with no previous list to go by - whatever is stored.
  char key[4];
  if (previous != nullptr) {
    for (const auto& old : previous->credentialsList) {
      if (findById(credentialsList, old.id) == nullptr) {
        credentialKey(old.id, key);
        if (!handle->Erase(key)) return false;
      }
    }
  } else {
    std::vector<std::string> stale;
    if (!handle->ForEachKey(
          NVS_TYPE_ANY,
          [&](const char* stored) {
      auto id = Convert::ToUint8(stored);
      if (!id || findById(credentialsList, *id) == nullptr) stale.emplace_back(stored);
    }
        ))
    {
      return false;
    }
    for (const auto& stored : stale) {
      if (!handle->Erase(stored.c_str())) return false;
    }
  }

  for (const auto& creds : credentialsList) {
    const WiFiCredentials* old = previous != nullptr ? findById(previous->credentialsList, creds.id) : nullptr;
    if (old != nullptr && *old == creds) {
      continue;
    }
    credentialKey(creds.id, key);
    if (!handle->SetBlob(key, creds.ToBlob())) {
      return false;
    }
  }
  return handle->Commit();
}

// 802.11 SSIDs are 1-32 bytes; WPA passphrases are up to 63 characters, or 64 for a raw hex PSK.
static bool isValidWiFiCredentials(std::string_view ssid, std::string_view password)
{
  if (ssid.empty() || ssid.size() > 32) {
    OS_LOGE(TAG, "Invalid SSID length %zu", ssid.size());
    return false;
  }
  if (password.size() > 64) {
    OS_LOGE(TAG, "Invalid password length %zu", password.size());
    return false;
  }
  return true;
}

WiFiConfig OpenShock::Config::GetWiFiConfig()
{
  return s_section.Get();
}

bool OpenShock::Config::SetWiFiConfig(const WiFiConfig& config)
{
  return s_section.Set(config);
}

std::vector<WiFiCredentials> OpenShock::Config::GetWiFiCredentials()
{
  return s_section.Read([](const WiFiConfig& wifi) { return wifi.credentialsList; });
}

void OpenShock::Config::GetWiFiCredentials(json_gen_str_t* gen, bool withSensitiveData)
{
  s_section.Read([&](const WiFiConfig& wifi) {
    for (const auto& creds : wifi.credentialsList) {
      creds.ToJSON(gen, nullptr, withSensitiveData);
    }
  });
}

bool OpenShock::Config::HasWiFiCredentials()
{
  return s_section.Read([](const WiFiConfig& wifi) { return !wifi.credentialsList.empty(); });
}

bool OpenShock::Config::SetWiFiCredentials(const std::vector<WiFiCredentials>& credentials)
{
  // An id of 0 means "pick one" and Normalize hands it the lowest free id; a repeated id is two networks under one key
  std::bitset<256> seenIds;
  for (const auto& creds : credentials) {
    if (creds.id != 0 && seenIds[creds.id]) {
      OS_LOGE(TAG, "Cannot set WiFi credentials: duplicate credential ID %u", creds.id);
      return false;
    }
    seenIds[creds.id] = true;

    if (!isValidWiFiCredentials(creds.ssid, creds.password)) {
      return false;
    }
  }

  return s_section.Update([&](WiFiConfig& wifi) {
    wifi.credentialsList = credentials;
    return true;
  });
}

bool OpenShock::Config::AnyWiFiCredentials(Util::FunctionRef<bool(const Config::WiFiCredentials&)> predicate)
{
  // The predicate is caller code, so run it on a copy outside the lock
  auto creds = GetWiFiCredentials();
  return std::any_of(creds.begin(), creds.end(), predicate);
}

uint8_t OpenShock::Config::AddWiFiCredentials(std::string_view ssid, std::string_view password, wifi_auth_mode_t authMode)
{
  if (!isValidWiFiCredentials(ssid, password)) {
    return 0;
  }

  uint8_t id  = 0;
  bool stored = s_section.Update([&](WiFiConfig& wifi) {
    auto& list = wifi.credentialsList;

    if (WiFiCredentials* existing = findBySsid(list, ssid)) {
      existing->password = password;
      if (authMode != WIFI_AUTH_MAX) {
        existing->authMode = authMode;
      }
      id = existing->id;
      return true;
    }

    id = lowestFreeId(list);
    if (id == 0) {
      OS_LOGE(TAG, "Failed to add WiFi credentials: no available IDs");
      return false;
    }

    list.emplace_back(id, ssid, password, authMode);
    return true;
  });

  // Only credentials that made it to flash count. The partition is the cap: once NVS has no room for another blob
  // the save fails and the list stays as it was.
  if (!stored) {
    OS_LOGE(TAG, "Failed to persist WiFi credentials for SSID %.*s", static_cast<int>(ssid.size()), ssid.data());
    return 0;
  }
  return id;
}

std::optional<WiFiCredentials> OpenShock::Config::TryGetWiFiCredentialsByID(uint8_t id)
{
  return s_section.Read([&](const WiFiConfig& wifi) -> std::optional<WiFiCredentials> {
    const WiFiCredentials* creds = findById(wifi.credentialsList, id);
    return creds != nullptr ? std::optional(*creds) : std::nullopt;
  });
}

std::optional<WiFiCredentials> OpenShock::Config::TryGetWiFiCredentialsBySSID(const char* ssid)
{
  return s_section.Read([&](const WiFiConfig& wifi) -> std::optional<WiFiCredentials> {
    const WiFiCredentials* creds = findBySsid(wifi.credentialsList, ssid);
    return creds != nullptr ? std::optional(*creds) : std::nullopt;
  });
}

uint8_t OpenShock::Config::GetWiFiCredentialsIDbySSID(const char* ssid)
{
  return s_section.Read([&](const WiFiConfig& wifi) -> uint8_t {
    const WiFiCredentials* creds = findBySsid(wifi.credentialsList, ssid);
    return creds != nullptr ? creds->id : 0;
  });
}

bool OpenShock::Config::PinWiFiCredentialsBSSID(uint8_t id, const uint8_t (&bssid)[6])
{
  bool found = false;
  bool saved = s_section.Update([&](WiFiConfig& wifi) {
    WiFiCredentials* creds = findById(wifi.credentialsList, id);
    if (creds == nullptr) {
      return false;
    }
    memcpy(creds->bssid.data(), bssid, 6);
    found = true;
    return true;
  });
  return found && saved;
}

bool OpenShock::Config::RemoveWiFiCredentials(uint8_t id)
{
  bool found = false;
  bool saved = s_section.Update([&](WiFiConfig& wifi) {
    auto& list                   = wifi.credentialsList;
    const WiFiCredentials* creds = findById(list, id);
    if (creds == nullptr) {
      return false;
    }
    list.erase(list.begin() + (creds - list.data()));
    found = true;
    return true;
  });
  return found && saved;
}

bool OpenShock::Config::ClearWiFiCredentials()
{
  return s_section.Update([](WiFiConfig& wifi) {
    wifi.credentialsList.clear();
    return true;
  });
}

std::string OpenShock::Config::GetWiFiHostname()
{
  return s_section.Read([](const WiFiConfig& wifi) { return wifi.hostname; });
}

bool OpenShock::Config::SetWiFiHostname(std::string hostname)
{
  return s_section.Update([&](WiFiConfig& wifi) {
    wifi.hostname = std::move(hostname);
    return true;
  });
}
