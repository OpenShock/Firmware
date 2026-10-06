#pragma once

// Reference RF frames, built with the same protocols encoders the transmitter uses,
// to classify what the fake RMT recorded.
#include "unity.h"

#include "host_fakes.h"
#include "radio/rmt/Sequence.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace TestFrames {
  inline std::vector<uint32_t> Symbols(const rmt_symbol_word_t* data, size_t count)
  {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < count; i++) {
      out.push_back(data[i].val);
    }
    return out;
  }

  /// @brief The frame sent while a command is live.
  inline std::vector<uint32_t> Payload(OpenShock::ShockerModelType model, uint16_t shockerId, OpenShock::ShockerCommandType type, uint8_t intensity)
  {
    OpenShock::Rmt::Sequence seq(model, shockerId, 0);
    TEST_ASSERT_TRUE(seq.is_valid());
    TEST_ASSERT_TRUE(seq.fill(type, intensity));
    return Symbols(seq.payload(), seq.size());
  }

  /// @brief The frame sent after a command ends (or is cut off) to stop the shocker.
  inline std::vector<uint32_t> Terminator(OpenShock::ShockerModelType model, uint16_t shockerId)
  {
    OpenShock::Rmt::Sequence seq(model, shockerId, 0);
    TEST_ASSERT_TRUE(seq.is_valid());
    return Symbols(seq.terminator(), seq.size());
  }

  inline size_t Count(const std::vector<uint32_t>& frame)
  {
    size_t n = 0;
    for (const auto& tx : HostFake::Transmissions) {
      if (tx.symbols == frame) {
        n++;
      }
    }
    return n;
  }
}  // namespace TestFrames
