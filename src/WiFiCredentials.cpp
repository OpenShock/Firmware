#include "WiFiCredentials.h"

#include <Arduino.h>
#include <LittleFS.h>

#include <esp_log.h>

#include <cstring>

const char* const TAG = "WiFiCredentials";

using namespace OpenShock;

bool WiFiCredentials::Load(std::vector<WiFiCredentials>& credentials) {
  credentials.clear();

  // Ensure the /wifi/creds directory exists
  if (!LittleFS.exists("/wifi/creds")) {
    if (!LittleFS.exists("/wifi")) {
      if (!LittleFS.mkdir("/wifi")) {
        ESP_LOGE(TAG, "Failed to create /wifi directory");
        return false;
      }
    }
    if (!LittleFS.mkdir("/wifi/creds")) {
      ESP_LOGE(TAG, "Failed to create /wifi/creds directory");
      return false;
    }
    ESP_LOGI(TAG, "No credentials directory found, created one");
    return true;
  }

  File credsDir = LittleFS.open("/wifi/creds");
  if (!credsDir) {
    ESP_LOGE(TAG, "Failed to open /wifi/creds");
    return false;
  }

  while (true) {
    File file = credsDir.openNextFile();
    if (!file) {
      break;
    }

    WiFiCredentials creds;
    if (!creds._load(file)) {
      ESP_LOGE(TAG, "Failed to load credentials from %s", file.name());
      continue;
    }

    credentials.push_back(creds);
  }

  return true;
}

WiFiCredentials::WiFiCredentials(std::uint8_t id, const wifi_ap_record_t* record) {
  _id = id;

  static_assert(sizeof(_ssid) == sizeof(record->ssid), "WiFiCredentials::_ssid and wifi_ap_record_t::ssid must be the same size");
  memset(_password, 0, sizeof(_password));
  memcpy(_ssid, record->ssid, sizeof(_ssid));
}

WiFiCredentials::WiFiCredentials(std::uint8_t id, const String& ssid, const String& password) {
  _id = id;

  auto ssidLength = ssid.length();
  if (ssid.length() > sizeof(_ssid) - 1) {
    ESP_LOGW(TAG, "SSID is too long, truncating");
    ssidLength = sizeof(_ssid) - 1;
  }
  _ssidLength = ssidLength;
  ssid.toCharArray(reinterpret_cast<char*>(_ssid), _ssidLength + 1);

  auto passwordLength = password.length();
  if (password.length() > sizeof(_password - 1)) {
    ESP_LOGW(TAG, "Password is too long, truncating");
    passwordLength = sizeof(_password) - 1;
  }
  _passwordLength = passwordLength;
  password.toCharArray(reinterpret_cast<char*>(_password), _passwordLength + 1);
}

void WiFiCredentials::setSSID(const String& ssid) {
  auto ssidLength = ssid.length();
  if (ssid.length() > sizeof(_ssid)) {
    ESP_LOGW(TAG, "SSID is too long, truncating");
    ssidLength = sizeof(_ssid) - 1;
  }
  _ssidLength = ssidLength;
  ssid.toCharArray(reinterpret_cast<char*>(_ssid), _ssidLength + 1);
}

void WiFiCredentials::setSSID(const std::uint8_t* ssid, std::size_t ssidLength) {
  if (ssidLength > sizeof(_ssid)) {
    ESP_LOGW(TAG, "SSID is too long, truncating");
    ssidLength = sizeof(_ssid) - 1;
  }
  _ssidLength = ssidLength;
  memcpy(_ssid, ssid, _ssidLength);
  _ssid[_ssidLength] = 0;
}

void WiFiCredentials::setPassword(const std::uint8_t* password, std::size_t passwordLength) {
  if (passwordLength > sizeof(_password)) {
    ESP_LOGW(TAG, "Password is too long, truncating");
    passwordLength = sizeof(_password) - 1;
  }
  _passwordLength = passwordLength;
  memcpy(_password, password, _passwordLength);
  _password[_passwordLength] = 0;
}

void WiFiCredentials::setPassword(const String& password) {
  auto passwordLength = password.length();
  if (password.length() > sizeof(_password)) {
    ESP_LOGW(TAG, "Password is too long, truncating");
    passwordLength = sizeof(_password) - 1;
  }
  _passwordLength = passwordLength;
  password.toCharArray(reinterpret_cast<char*>(_password), _passwordLength + 1);
}

bool WiFiCredentials::save() const {
  char filename[16] = {0};
  sprintf(filename, "/wifi/creds/%u", _id);
  File file = LittleFS.open(filename, "wb");
  if (!file) {
    ESP_LOGE(TAG, "Failed to open file %s for writing", filename);
    return false;
  }

  file.write(_id);
  file.write(reinterpret_cast<const std::uint8_t*>(&_ssidLength), sizeof(_ssidLength));
  file.write(_ssid, _ssidLength);
  file.write(reinterpret_cast<const std::uint8_t*>(&_passwordLength), sizeof(_passwordLength));
  file.write(_password, _passwordLength);

  file.close();

  return true;
}

bool WiFiCredentials::erase() const {
  char filename[16] = {0};
  sprintf(filename, "/wifi/creds/%u", _id);
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

  file.read(reinterpret_cast<std::uint8_t*>(&_id), sizeof(_id));
  if (_id > 31) {
    const char* filename = file.name();
    ESP_LOGE(TAG, "Loading credentials for %s failed: ID is too large (needs to fit into a uint32 by bitshifting)", filename);  // Look in WiFiManager.cpp for the bitshifting
    ESP_LOGW(TAG, "Deleting credentials for %s", filename);
    LittleFS.remove(filename);
    return false;
  }

  file.read(reinterpret_cast<std::uint8_t*>(&_ssidLength), sizeof(_ssidLength));
  if (_ssidLength > sizeof(_ssid) - 1) {
    const char* filename = file.name();
    ESP_LOGE(TAG, "Loading credentials for %s failed: SSID length is too long", filename);
    ESP_LOGW(TAG, "Deleting credentials for %s", filename);
    LittleFS.remove(filename);
    return false;
  }
  file.read(_ssid, _ssidLength);
  _ssid[_ssidLength] = 0;

  file.read(reinterpret_cast<std::uint8_t*>(&_passwordLength), sizeof(_passwordLength));
  if (_passwordLength > sizeof(_password) - 1) {
    const char* filename = file.name();
    ESP_LOGE(TAG, "Loading credentials for %s failed: password length is too long", filename);
    ESP_LOGW(TAG, "Deleting credentials for %s", filename);
    LittleFS.remove(filename);
    return false;
  }
  file.read(_password, _passwordLength);
  _password[_passwordLength] = 0;

  return true;
}
