#pragma once

#include "http/HTTPError.h"

namespace OpenShock::HTTP {
  template<typename T>
  struct [[nodiscard]] ReadResult {
    HTTPError error{};
    T data{};

    ReadResult(const T& d)
      : error(HTTPError::None), data(d) {}

    ReadResult(const HTTPError& e)
      : error(e), data{} {}
  };
}  // namespace OpenShock::HTTP
