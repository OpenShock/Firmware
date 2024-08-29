#pragma once

#include "StringView.h"

#include <cstdint>

namespace OpenShock::IntConv {
    bool stoi8(OpenShock::StringView str, std::int8_t& val);
    bool stou8(OpenShock::StringView str, std::uint8_t& val);
    bool stoi16(OpenShock::StringView str, std::int16_t& val);
    bool stou16(OpenShock::StringView str, std::uint16_t& val);
    bool stoi32(OpenShock::StringView str, std::int32_t& val);
    bool stou32(OpenShock::StringView str, std::uint32_t& val);
    bool stoi64(OpenShock::StringView str, std::int64_t& val);
    bool stou64(OpenShock::StringView str, std::uint64_t& val);
}