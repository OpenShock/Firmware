#pragma once

#include "Common.h"
#include "http/HTTPResponse.h"

#include <esp_err.h>
#include <esp_http_client.h>

#include <cstdint>
#include <string>

namespace OpenShock::HTTP {
  class HTTPClient {
    DISABLE_DEFAULT(HTTPClient);
    DISABLE_COPY(HTTPClient);

  public:
    HTTPClient(const char* url, int timeout_ms = 10'000)
    {
      esp_http_client_config_t cfg = {
        .url                   = url,
        .user_agent            = OpenShock::Constants::FW_USERAGENT,
        .timeout_ms            = timeout_ms,
        .disable_auto_redirect = true,
        .is_async              = true,
        .use_global_ca_store   = true,
      };
      handle = esp_http_client_init(&cfg);
    }
    ~HTTPClient()
    {
      if (handle != nullptr) {
        esp_http_client_cleanup(handle);
      }
    }
    HTTPClient(HTTPClient&& other)
    {
      handle       = other.handle;
      other.handle = nullptr;
    }

    constexpr bool IsClosed() const { return handle == nullptr; }

    bool SetHeader(const char* key, const char* value)
    {
      if (handle == nullptr) return false;

      return esp_http_client_set_header(handle, key, value) == ESP_OK;
    }
    bool RemoveHeader(const char* key)
    {
      if (handle == nullptr) return false;

      return esp_http_client_delete_header(handle, key) == ESP_OK;
    }

    HTTPResponse Send(const char* url, esp_http_client_method_t method, int contentLength = 0)
    {
      esp_err_t err = ESP_FAIL;
      if (handle == nullptr) {
        return HTTPResponse(err);
      }

      err = esp_http_client_set_method(handle, method);
      if (err != ESP_OK) return HTTPResponse(err);

      err = esp_http_client_set_url(handle, url);
      if (err != ESP_OK) return HTTPResponse(err);

      err = esp_http_client_open(handle, contentLength);
      if (err != ESP_OK) return HTTPResponse(err);

      std::int64_t respContentLength = esp_http_client_fetch_headers(handle);
      if (respContentLength < 0) return HTTPResponse(ESP_FAIL);

      bool isChunked = false;
      if (respContentLength == 0) {
        isChunked = esp_http_client_is_chunked_response(handle);
      }

      return HTTPResponse(handle, respContentLength, isChunked);
    }

    HTTPResponse Get(const char* url) { return Send(url, HTTP_METHOD_GET); }

  private:
    esp_http_client_handle_t handle;
  };
}  // namespace OpenShock::HTTP
