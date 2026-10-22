#pragma once

#include "flatbuffers/flatbuffers.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace OpenShock::Serialization {
  /// @brief Verifies an untrusted FlatBuffer and returns its root, or nullptr if it is too small, larger than
  ///        `maxSize`, or fails verification.
  /// The size is checked before constructing the Verifier: its constructor asserts size < max_size, so an oversized
  /// buffer would abort instead of being rejected.
  template<typename T>
  const T* GetVerifiedRoot(std::span<const uint8_t> data, std::size_t maxSize)
  {
    if (data.size() < sizeof(flatbuffers::uoffset_t) || data.size() > maxSize) {
      return nullptr;
    }

    flatbuffers::Verifier::Options options {
      .max_size = maxSize + 1,  // Verifier requires size < max_size
    };
    flatbuffers::Verifier verifier(data.data(), data.size(), options);
    if (!verifier.VerifyBuffer<T>()) {
      return nullptr;
    }

    return flatbuffers::GetRoot<T>(data.data());
  }
}  // namespace OpenShock::Serialization
