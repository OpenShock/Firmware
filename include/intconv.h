#pragma once

#include "StringView.h"

#include <cstdint>

namespace OpenShock::IntConv {
    bool stoi8(OpenShock::StringView str, int8_t& val);
    bool stou8(OpenShock::StringView str, uint8_t& val);
    bool stoi16(OpenShock::StringView str, int16_t& val);
    bool stou16(OpenShock::StringView str, uint16_t& val);
    bool stoi32(OpenShock::StringView str, int32_t& val);
    bool stou32(OpenShock::StringView str, uint32_t& val);
    bool stoi64(OpenShock::StringView str, int64_t& val);
    bool stou64(OpenShock::StringView str, uint64_t& val);
}