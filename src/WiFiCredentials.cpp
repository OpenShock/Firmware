#include "WiFiCredentials.h"

#include "Utils/HexUtils.h"

#include <LittleFS.h>

#include <esp_log.h>

#include <cstring>

const char* const TAG = "WiFiCredentials";

using namespace OpenShock;

const char* const WiFiDir      = "/wifi";
const char* const WiFiCredsDir = "/wifi/creds";

inline void GetWiFiCredsFilename(char (&filename)[15], std::uint8_t id) {
  memcpy(filename, WiFiCredsDir, 11);
  filename[11] = '/';
  HexUtils::ToHex(id, filename + 12);
  filename[14] = 0;
}

template<std::uint8_t N>
std::uint8_t CopyString(const char* src, std::uint8_t srcLength, char (&dest)[N]) {
  if (src == nullptr || srcLength == 0) {
    ESP_LOGW(TAG, "String is null/empty, clearing");
    memset(dest, 0, N);
    return 0;
  }

  if (srcLength > N - 1) {
    ESP_LOGW(TAG, "String is too long, truncating");
    srcLength = N - 1;
  }

  memcpy(dest, src, srcLength);
  dest[srcLength] = 0;

  return srcLength;
}

template<std::uint8_t N>
bool WriteString(fs::File& file, const char (&str)[N], std::uint8_t length) {
  if (length > N - 1) return false;

  file.write(&length, sizeof(length));
  file.write(reinterpret_cast<const std::uint8_t*>(str), length);

  return true;
}
template<std::uint8_t N>
bool ReadString(fs::File& file, char (&str)[N], std::uint8_t& length) {
  file.read(&length, sizeof(length));

  if (length > N - 1) return false;

  file.read(reinterpret_cast<std::uint8_t*>(str), length);

  str[length] = 0;  // Ensure null-terminated

  return true;
}

bool WiFiCredentials::Load(std::vector<WiFiCredentials>& credentials) {
  credentials.clear();

  return true;
}

WiFiCredentials::WiFiCredentials(std::uint8_t id, const char* ssid, std::uint8_t ssidLength, const char* password, std::uint8_t passwordLength) {
  _id = id;

  setSSID(ssid, ssidLength);
  setPassword(password, passwordLength);
}

void WiFiCredentials::setSSID(const char* ssid, std::uint8_t ssidLength) {
  _ssidLength = CopyString(ssid, ssidLength, _ssid);
}

void WiFiCredentials::setPassword(const char* password, std::uint8_t passwordLength) {
  _passwordLength = CopyString(password, passwordLength, _password);
}

bool WiFiCredentials::save() const {
  if (!LittleFS.exists(WiFiCredsDir)) {
    LittleFS.mkdir(WiFiDir);
    LittleFS.mkdir(WiFiCredsDir);
  }

  char filename[15];
  GetWiFiCredsFilename(filename, _id);
  File file = LittleFS.open(filename, "wb");
  if (!file) {
    ESP_LOGE(TAG, "Failed to open file %s for writing", filename);
    return false;
  }

  file.write(_id);
  if (!WriteString(file, _ssid, _ssidLength)) return false;
  if (!WriteString(file, _password, _passwordLength)) return false;

  file.close();

  return true;
}

bool WiFiCredentials::erase() const {
  char filename[15];
  GetWiFiCredsFilename(filename, _id);
  if (!LittleFS.remove(filename)) {
    ESP_LOGE(TAG, "Failed to remove file %s", filename);
    return false;
  }

  return true;
}

bool WiFiCredentials::_load(fs::File& file) {
  if (!file) {
    ESP_LOGE(TAG, "File is not open");
    return false;
  }

  file.read(&_id, sizeof(_id));
  if (_id > 31) {
    const char* filename = file.name();
    ESP_LOGE(TAG, "Loading credentials for %s failed: ID is too large (needs to fit into a uint32 by bitshifting)", filename);  // Look in WiFiManager.cpp for the bitshifting
    ESP_LOGW(TAG, "Deleting credentials for %s", filename);
    LittleFS.remove(filename);
    return false;
  }

  if (!ReadString(file, _ssid, _ssidLength)) {
    const char* filename = file.name();
    ESP_LOGE(TAG, "Loading credentials for %s failed: SSID length is too long", filename);
    ESP_LOGW(TAG, "Deleting credentials for %s", filename);
    LittleFS.remove(filename);
    return false;
  }

  if (!ReadString(file, _password, _passwordLength)) {
    const char* filename = file.name();
    ESP_LOGE(TAG, "Loading credentials for %s failed: password length is too long", filename);
    ESP_LOGW(TAG, "Deleting credentials for %s", filename);
    LittleFS.remove(filename);
    return false;
  }

  return true;
}
