#pragma once

#include <esp_err.h>
#include <esp_http_client.h>

#include <cstdint>
#include <string>

namespace OpenShock::HTTP {
  class HTTPClient;
  class HTTPResponse {
    DISABLE_DEFAULT(HTTPResponse);
    DISABLE_COPY(HTTPResponse);

    friend class HTTPClient;

    HTTPResponse(esp_err_t error)
      : m_handle(nullptr)
      , m_error(error)
    {
    }
    HTTPResponse(esp_http_client_handle_t handle, std::int64_t contentLength, bool isChunked)
      : m_handle(handle)
      , m_contentLength(contentLength)
      , m_isChunked(isChunked)
    {
      if (m_handle == nullptr) {
        m_error = ESP_FAIL;
        return;
      }
    }

  public:
    HTTPResponse(HTTPResponse&& other)
    {
      m_handle       = other.m_handle;
      other.m_handle = nullptr;
    }

    constexpr bool IsValid() const { return m_handle != nullptr; }
    constexpr esp_err_t GetError() const { return m_handle == nullptr ? m_error : ESP_OK; }

    int ResponseCode() const { return esp_http_client_get_status_code(m_handle); }

  private:
    esp_http_client_handle_t m_handle;
    union {
      struct {  // Only valid if m_handle is not nullptr.
        std::int64_t m_contentLength;
        bool m_isChunked;
      };
      esp_err_t m_error;  // If m_handle is nullptr, this is the error code.
    };
  };
}  // namespace OpenShock::HTTP
