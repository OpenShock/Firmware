#pragma once

#include <cstddef>
#include <cstdint>

namespace OpenShock::Diagnostics {
  /// @brief Bookkeeping of a RetainedRing. Lives next to the ring's bytes in memory that survives a software reset
  ///        (__NOINIT_ATTR), so a crash keeps both the lines that led up to it and how far they were consumed.
  struct RetainedRingState {
    uint32_t magic;
    uint32_t capacity;
    uint64_t written;   // Bytes ever appended
    uint64_t consumed;  // Bytes the consumer has taken; never more than `written`
    uint32_t check;     // Cross-check of the fields above, so stale memory after a power-up is not trusted
  };

  /// @brief A byte ring that overwrites its oldest bytes, with one consumer that takes bytes in order.
  ///        Positions are absolute byte counts, so the consumer can tell how much it missed while the ring overflowed.
  ///        Not synchronized: the owner serializes every call.
  class RetainedRing {
  public:
    RetainedRing(RetainedRingState& state, char* data, uint32_t capacity) noexcept;

    /// @brief Keeps what the memory held before this boot if its state is intact, otherwise starts empty.
    /// @return Whether earlier content was kept.
    bool Adopt();
    void Reset();

    void Append(const char* data, std::size_t len);

    uint64_t Written() const { return m_state.written; }
    /// @brief Bytes appended but not yet consumed, including any already overwritten.
    uint64_t Pending() const { return m_state.written - m_state.consumed; }

    uint64_t Consumed() const { return m_state.consumed; }

    /// @brief Copies bytes from absolute position `start` on (or the oldest still in the ring, if those are gone) into
    ///        `out`, up to what has been written.
    /// @param from  Set to the absolute position of out[0]: more than `start` when bytes were overwritten.
    /// @return The number of bytes copied.
    std::size_t Peek(uint64_t start, char* out, std::size_t outCapacity, uint64_t& from) const;

    /// @brief Marks everything before absolute position `upTo` as consumed.
    void Consume(uint64_t upTo);

  private:
    void seal();
    uint32_t computeCheck() const;

    RetainedRingState& m_state;
    char* m_data;
    uint32_t m_capacity;
  };
}  // namespace OpenShock::Diagnostics
