#pragma once

namespace OpenShock::HTTP {
  enum class HTTPError {
    None,
    InternalError,
    RateLimited,
    InvalidUrl,
    InvalidHttpMethod,
    NetworkError,
    ConnectionClosed,
    SizeLimitExceeded,
    Aborted,
    ParseFailed
  };

  inline const char* HTTPErrorToString(HTTPError error) {
    switch (error)
    {
    case HTTPError::None:
      return "";
    case HTTPError::InternalError:
      return "";
    case HTTPError::RateLimited:
      return "";
    case HTTPError::InvalidUrl:
      return "";
    case HTTPError::InvalidHttpMethod:
      return "";
    case HTTPError::NetworkError:
      return "";
    case HTTPError::ConnectionClosed:
      return "";
    case HTTPError::SizeLimitExceeded:
      return "";
    case HTTPError::Aborted:
      return "";
    case HTTPError::ParseFailed:
      return "";
    default:
      return "";
    }
  }
} // namespace OpenShock::HTTP
