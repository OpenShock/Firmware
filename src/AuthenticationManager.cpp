#include "AuthenticationManager.h"

#include "Constants.h"

#include <HTTPClient.h>
#include <LittleFS.h>

static const char* const TAG             = "AuthenticationManager";
static const char* const AUTH_TOKEN_FILE = "/authToken";

bool TryWriteFile(const char* path, const std::uint8_t* data, std::size_t length) {
  if (LittleFS.exists(path)) {
    LittleFS.remove(path);
  }

  File file = LittleFS.open(path, FILE_WRITE);
  if (!file) {
    ESP_LOGE(TAG, "Failed to open file %s", path);

    return false;
  }

  file.write(data, length);
  file.close();

  return true;
}
bool TryWriteFile(const char* path, const String& str) {
  return TryWriteFile(
    path,
    reinterpret_cast<const std::uint8_t*>(str.c_str()),
    str.length() + 1);  // WARNING: Assumes null-terminated string, could be dangerous (But in practice I think it's fine)
}
bool TryReadFile(const char* path, String& str) {
  File file = LittleFS.open(path, FILE_READ);
  if (!file) {
    ESP_LOGE(TAG, "Failed to open file %s", path);

    return false;
  }

  str = file.readString();
  file.close();

  return true;
}

static bool _isAuthenticated = false;
static String authToken;

bool ShockLink::AuthenticationManager::Authenticate(unsigned int pairCode) {
  HTTPClient http;
  String uri = SHOCKLINK_API_URL("/1/device/pair/") + String(pairCode);

  ESP_LOGD(TAG, "Contacting pair code url: %s", uri.c_str());
  http.begin(uri);

  int responseCode = http.GET();

  if (responseCode != 200) {
    ESP_LOGE(TAG, "Error while getting auth token: [%d] %s", responseCode, http.getString().c_str());

    _isAuthenticated = false;
    return false;
  }

  authToken = http.getString();

  if (!TryWriteFile(AUTH_TOKEN_FILE, authToken)) {
    ESP_LOGE(TAG, "Error while writing auth token to file");

    _isAuthenticated = false;
    return false;
  }

  http.end();

  _isAuthenticated = true;

  return true;
}

bool ShockLink::AuthenticationManager::IsAuthenticated() {
  if (_isAuthenticated) {
    return true;
  }

  if (!TryReadFile(AUTH_TOKEN_FILE, authToken)) {
    return false;
  }

  HTTPClient http;
  const char* const uri = SHOCKLINK_API_URL("/1/device/self");

  ESP_LOGD(TAG, "Contacting self url: %s", uri);
  http.begin(uri);

  int responseCode = http.GET();

  if (responseCode != 200) {
    ESP_LOGE(TAG, "Error while verifying auth token: [%d] %s", responseCode, http.getString().c_str());
    LittleFS.remove(AUTH_TOKEN_FILE);
    return false;
  }

  http.end();

  _isAuthenticated = true;

  ESP_LOGD(TAG, "Successfully verified auth token");

  return true;
}

String ShockLink::AuthenticationManager::GetAuthToken() {
  if (_isAuthenticated) {
    return authToken;
  }

  if (!TryReadFile(AUTH_TOKEN_FILE, authToken)) {
    return "";
  }

  _isAuthenticated = true;

  return authToken;
}

void ShockLink::AuthenticationManager::ClearAuthToken() {
  authToken        = "";
  _isAuthenticated = false;

  LittleFS.remove(AUTH_TOKEN_FILE);
}
