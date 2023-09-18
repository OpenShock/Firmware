#include "AuthenticationManager.h"

#include "Constants.h"

#include <HTTPClient.h>
#include <LittleFS.h>

static const char *const TAG = "AuthenticationManager";
static const char *const AUTH_TOKEN_FILE = "/authToken";

bool TryWriteFile(const char *path, const std::uint8_t *data, std::size_t length)
{
    if (LittleFS.exists(path))
    {
        LittleFS.remove(path);
    }

    File file = LittleFS.open(path, FILE_WRITE);
    if (!file)
    {
        ESP_LOGE(TAG, "Failed to open file %s", path);

        return false;
    }

    file.write(data, length);
    file.close();

    return true;
}
bool TryWriteFile(const char *path, const String &str)
{
    return TryWriteFile(path, reinterpret_cast<const std::uint8_t *>(str.c_str()), str.length() + 1); // WARNING: Assumes null-terminated string, could be dangerous (But in practice I think it's fine)
}
bool TryReadFile(const char *path, String &str)
{
    File file = LittleFS.open(path, FILE_READ);
    if (!file)
    {
        ESP_LOGE(TAG, "Failed to open file %s", path);

        return false;
    }

    str = file.readString();
    file.close();

    return true;
}

static bool _isAuthenticated = false;
static String authToken;

bool ShockLink::AuthenticationManager::Authenticate(std::uint32_t pairCode)
{
    HTTPClient http;
    String uri = ShockLink::Constants::ApiPairUrl + String(pairCode);

    ESP_LOGI(TAG, "Contacting pair code url: %s", uri.c_str());
    http.begin(uri);

    int responseCode = http.GET();

    if (responseCode != 200)
    {
        ESP_LOGE(TAG, "Error while getting auth token: [%d] %s", responseCode, http.getString().c_str());

        _isAuthenticated = false;
        return false;
    }

    authToken = http.getString();

    if (!TryWriteFile(AUTH_TOKEN_FILE, authToken))
    {
        ESP_LOGE(TAG, "Error while writing auth token to file");

        _isAuthenticated = false;
        return false;
    }

    http.end();

    _isAuthenticated = true;

    return true;
}

bool ShockLink::AuthenticationManager::IsAuthenticated()
{
    if (_isAuthenticated)
    {
        return true;
    }

    if (!TryReadFile(AUTH_TOKEN_FILE, authToken))
    {
        return false;
    }

    _isAuthenticated = true;

    return true;
}

String ShockLink::AuthenticationManager::GetAuthToken()
{
    if (_isAuthenticated)
    {
        return authToken;
    }

    if (!TryReadFile(AUTH_TOKEN_FILE, authToken))
    {
        return "";
    }

    _isAuthenticated = true;

    return authToken;
}

void ShockLink::AuthenticationManager::ClearAuthToken()
{
    authToken = "";
    _isAuthenticated = false;

    LittleFS.remove(AUTH_TOKEN_FILE);
}