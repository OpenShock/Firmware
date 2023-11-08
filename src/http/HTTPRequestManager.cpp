#include "http/HTTPRequestManager.h"

#include <HTTPClient.h>

const char* const TAG = "HTTPRequestManager";

using namespace OpenShock;

const char* const USER_AGENT = OPENSHOCK_FW_HOSTNAME "/" OPENSHOCK_FW_VERSION " (Espressif; " OPENSHOCK_FW_CHIP "; " OPENSHOCK_FW_BOARD ") " OPENSHOCK_FW_COMMIT;

bool HTTPRequestManager::Init() {
  return true;
}

HTTPRequestManager::Response<String> HTTPRequestManager::GetString(const HTTPRequestManager::Request& request) {
  // TODO: Implement rate limiting

  HTTPClient client;

  client.setUserAgent(USER_AGENT);

  if (!client.begin(request.url)) {
    ESP_LOGE(TAG, "Failed to begin HTTP request");
    return { RequestResult::RequestFailed, 0, "" };
  }

  for (auto& header : request.headers) {
    client.addHeader(header.first, header.second);
  }

  int responseCode = client.GET();

  if (responseCode == HTTP_CODE_TOO_MANY_REQUESTS) {
    // TODO: Implement rate limiting
  }

  if (responseCode == 418) {
    ESP_LOGW(TAG, "The server refused to brew coffee because it is, permanently, a teapot.");
  }

  if (responseCode >= 500) {
    return { RequestResult::ServerError, responseCode, "" };
  }

  if (!std::any_of(request.okCodes.begin(), request.okCodes.end(), [responseCode](int code) { return code == responseCode; })) {
    return { RequestResult::ResponseCodeNotOK, responseCode, "" };
  }

  return { RequestResult::Success, responseCode, client.getString() };
}
