#pragma once

namespace OpenShock::HTTP {
  enum class HTTPError {
    None,
    ClientBusy,
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
      return "None";
    case HTTPError::InternalError:
      return "InternalError";
    case HTTPError::RateLimited:
      return "RateLimited";
    case HTTPError::InvalidUrl:
      return "InvalidUrl";
    case HTTPError::InvalidHttpMethod:
      return "InvalidHttpMethod";
    case HTTPError::NetworkError:
      return "NetworkError";
    case HTTPError::ConnectionClosed:
      return "ConnectionClosed";
    case HTTPError::SizeLimitExceeded:
      return "SizeLimitExceeded";
    case HTTPError::Aborted:
      return "Aborted";
    case HTTPError::ParseFailed:
      return "ParseFailed";
    default:
      return "Unknown";
    }
  }
} // namespace OpenShock::HTTP
