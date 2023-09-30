#include "AuthenticationManager.h"

#include "Constants.h"
#include "FileUtils.h"

#include <HTTPClient.h>

static const char* const TAG             = "AuthenticationManager";
static const char* const AUTH_TOKEN_FILE = "/authToken";

static bool _isAuthenticated = false;
static String _authToken;

using namespace OpenShock;

bool AuthenticationManager::Authenticate(unsigned int pairCode) {
  HTTPClient http;
  String uri = OPENSHOCK_API_URL("/1/device/pair/") + String(pairCode);

  ESP_LOGD(TAG, "Contacting pair code url: %s", uri.c_str());
  http.begin(uri);

  int responseCode = http.GET();

  if (responseCode != 200) {
    ESP_LOGE(TAG, "Error while getting auth token: [%d] %s", responseCode, http.getString().c_str());

    _isAuthenticated = false;
    return false;
  }

  _authToken = http.getString();

  if (!FileUtils::TryWriteFile(AUTH_TOKEN_FILE, _authToken)) {
    ESP_LOGE(TAG, "Error while writing auth token to file");

    _isAuthenticated = false;
    return false;
  }

  http.end();

  _isAuthenticated = true;

  return true;
}

bool AuthenticationManager::IsAuthenticated() {
  if (_isAuthenticated) {
    return true;
  }

  if (!FileUtils::TryReadFile(AUTH_TOKEN_FILE, _authToken)) {
    return false;
  }

  HTTPClient http;
  const char* const uri = OPENSHOCK_API_URL("/1/device/self");

  ESP_LOGD(TAG, "Contacting self url: %s", uri);
  http.begin(uri);

  int responseCode = http.GET();

  if (responseCode != 200) {
    ESP_LOGE(TAG, "Error while verifying auth token: [%d] %s", responseCode, http.getString().c_str());
    FileUtils::DeleteFile(AUTH_TOKEN_FILE);
    return false;
  }

  http.end();

  _isAuthenticated = true;

  ESP_LOGD(TAG, "Successfully verified auth token");

  return true;
}

String AuthenticationManager::GetAuthToken() {
  if (_isAuthenticated) {
    return _authToken;
  }

  if (!FileUtils::TryReadFile(AUTH_TOKEN_FILE, _authToken)) {
    return "";
  }

  _isAuthenticated = true;

  return _authToken;
}

void AuthenticationManager::ClearAuthToken() {
  _isAuthenticated = false;
  _authToken       = "";

  FileUtils::DeleteFile(AUTH_TOKEN_FILE);
}
