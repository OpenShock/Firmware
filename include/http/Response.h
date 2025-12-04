#pragma once

#include "http/ResponseResult.h"

#include <esp_err.h>

namespace OpenShock::HTTP {
  template<typename T>
  struct [[nodiscard]] Response {
    ResponseResult result;
    esp_err_t error;
    T data;
  };
}  // namespace OpenShock::HTTP
