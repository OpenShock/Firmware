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

Section<WiFiConfig>& OpenShock::Config::WiFiSection()
{
  return s_section;
}

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
  if (!json.valid()) {
    OS_LOGW(TAG, "Config is null, setting to default");
    *this = WiFiConfig();
    return true;
  }

  if (!json.isObject()) {
    OS_LOGE(TAG, "json is not an object");
    return false;
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

  // Each credential is stored under its id, so 0 (what a missing id parses to) and a repeated id would be lost on
  // flash. Those get the lowest free id instead; one that finds none is dropped.
  std::bitset<256> usedIds;
  usedIds[0] = true;
  for (const auto& creds : credentialsList) {
    usedIds[creds.id] = true;
  }
  std::bitset<256> seenIds;
  for (auto it = credentialsList.begin(); it != credentialsList.end();) {
    if (it->id != 0 && !seenIds[it->id]) {
      seenIds[it->id] = true;
      ++it;
      continue;
    }
    std::size_t freeId = 1;
    while (freeId < usedIds.size() && usedIds[freeId]) {
      ++freeId;
    }
    if (freeId == usedIds.size()) {
      OS_LOGW(TAG, "Dropping credential %u: no free id left", it->id);
      it = credentialsList.erase(it);
      continue;
    }
    OS_LOGW(TAG, "Credential %u renumbered to %zu", it->id, freeId);
    it->id          = static_cast<uint8_t>(freeId);
    usedIds[freeId] = true;
    seenIds[freeId] = true;
    ++it;
  }

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

  const WiFiCredentials* findById(const std::vector<WiFiCredentials>& list, uint8_t id)
  {
    for (const auto& creds : list) {
      if (creds.id == id) return &creds;
    }
    return nullptr;
  }

  bool sameCredentials(const WiFiCredentials& a, const WiFiCredentials& b)
  {
    return a.id == b.id && a.ssid == b.ssid && a.password == b.password && a.authMode == b.authMode && a.bssid == b.bssid;
  }
}  // namespace

bool WiFiConfig::LoadNvs(const char* partition)
{
  if (auto handle = Nvs::Handle::Open(partition, kNamespaceCfg, false)) {
    if (!Nvs::Load(*handle, kKeyApSsid, accessPointSSID, CONFIG_OPENSHOCK_FW_AP_PREFIX) || !Nvs::Load(*handle, kKeyHostname, hostname, CONFIG_OPENSHOCK_FW_HOSTNAME)) {
      return false;
    }
  } else {
    accessPointSSID = CONFIG_OPENSHOCK_FW_AP_PREFIX;
    hostname        = CONFIG_OPENSHOCK_FW_HOSTNAME;
  }

  credentialsList.clear();
  auto handle = Nvs::Handle::Open(partition, kNamespaceCreds, false);
  if (!handle) {
    return true;  // No credential was ever stored
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
    if (!handle->SetStr(kKeyApSsid, accessPointSSID, previous != nullptr ? &previous->accessPointSSID : nullptr) || !handle->SetStr(kKeyHostname, hostname, previous != nullptr ? &previous->hostname : nullptr) || !handle->Commit()) {
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
    if (old != nullptr && sameCredentials(*old, creds)) {
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

std::optional<WiFiConfig> OpenShock::Config::GetWiFiConfig()
{
  return s_section.Get();
}

bool OpenShock::Config::SetWiFiConfig(const WiFiConfig& config)
{
  return s_section.Set(config);
}

std::optional<std::vector<WiFiCredentials>> OpenShock::Config::GetWiFiCredentials()
{
  return s_section.Read([](const WiFiConfig& wifi) { return wifi.credentialsList; });
}

bool OpenShock::Config::GetWiFiCredentials(json_gen_str_t* gen, bool withSensitiveData)
{
  s_section.Read([&](const WiFiConfig& wifi) {
    for (const auto& creds : wifi.credentialsList) {
      creds.ToJSON(gen, nullptr, withSensitiveData);
    }
  });
  return true;
}

bool OpenShock::Config::SetWiFiCredentials(const std::vector<WiFiCredentials>& credentials)
{
  std::bitset<256> seenIds;
  for (const auto& creds : credentials) {
    if (creds.id == 0) {
      OS_LOGE(TAG, "Cannot set WiFi credentials: credential ID cannot be 0");
      return false;
    }
    if (seenIds[creds.id]) {
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
  return std::any_of(creds->begin(), creds->end(), predicate);
}

uint8_t OpenShock::Config::AddWiFiCredentials(std::string_view ssid, std::string_view password, wifi_auth_mode_t authMode)
{
  if (!isValidWiFiCredentials(ssid, password)) {
    return 0;
  }

  uint8_t id  = 0;
  bool stored = s_section.Update([&](WiFiConfig& wifi) {
    auto& list = wifi.credentialsList;

    std::bitset<255> bits;
    for (auto it = list.begin(); it != list.end();) {
      auto& creds = *it;

      if (std::string_view(creds.ssid) == ssid) {
        creds.password = password;
        if (authMode != WIFI_AUTH_MAX) {
          creds.authMode = authMode;
        }
        id = creds.id;
        return true;
      }

      if (creds.id == 0) {
        OS_LOGW(TAG, "Found WiFi credentials with ID 0, removing");
        it = list.erase(it);
        continue;
      }

      // Mark ID as used
      bits[creds.id - 1] = true;
      ++it;
    }

    // First available ID
    for (std::size_t i = 0; i < bits.size(); ++i) {
      if (!bits[i]) {
        id = static_cast<uint8_t>(i + 1);
        break;
      }
    }
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
    for (const auto& creds : wifi.credentialsList) {
      if (creds.id == id) {
        return creds;
      }
    }
    return std::nullopt;
  });
}

std::optional<WiFiCredentials> OpenShock::Config::TryGetWiFiCredentialsBySSID(const char* ssid)
{
  return s_section.Read([&](const WiFiConfig& wifi) -> std::optional<WiFiCredentials> {
    for (const auto& creds : wifi.credentialsList) {
      if (creds.ssid == ssid) {
        return creds;
      }
    }
    return std::nullopt;
  });
}

uint8_t OpenShock::Config::GetWiFiCredentialsIDbySSID(const char* ssid)
{
  return s_section.Read([&](const WiFiConfig& wifi) -> uint8_t {
    for (const auto& creds : wifi.credentialsList) {
      if (creds.ssid == ssid) {
        return creds.id;
      }
    }
    return 0;
  });
}

bool OpenShock::Config::PinWiFiCredentialsBSSID(uint8_t id, const uint8_t (&bssid)[6])
{
  bool found = false;
  bool saved = s_section.Update([&](WiFiConfig& wifi) {
    for (auto& creds : wifi.credentialsList) {
      if (creds.id == id) {
        memcpy(creds.bssid.data(), bssid, 6);
        found = true;
        return true;
      }
    }
    return false;
  });
  return found && saved;
}

bool OpenShock::Config::RemoveWiFiCredentials(uint8_t id)
{
  bool found = false;
  bool saved = s_section.Update([&](WiFiConfig& wifi) {
    auto& list = wifi.credentialsList;
    auto it    = std::find_if(list.begin(), list.end(), [id](const WiFiCredentials& creds) { return creds.id == id; });
    if (it == list.end()) {
      return false;
    }
    list.erase(it);
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

std::optional<std::string> OpenShock::Config::GetWiFiHostname()
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
