#pragma once

// Typed access to one NVS namespace: what every module's config section uses for its LoadNvs / SaveNvs. The
// partition is the one the store picked (Config::StorageLabel(), handed to LoadNvs/SaveNvs), never hardcoded.
//
// Reads are tri-state (Found / Missing / Error): a missing key is a default, an error fails the load. A load opens
// its namespace with OpenForLoad: a namespace never written is an empty handle on which every key is Missing, so a
// fresh store reads as the defaults, while an open that failed is nullopt and fails the load. Writes go through
// Saver, which skips the keys whose value did not change: NVS is append-only with copy-on-write pages, so every write
// costs an entry until the page is compacted, and a field that did not change costs nothing. NVS guarantees atomicity
// per key, not across keys: anything that must change together belongs in one blob.
//
// Namespace and key names are at most 15 characters and are effectively permanent once devices hold them.

#include <nvs.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace OpenShock::Config::Nvs {
  enum class Result : uint8_t {
    Found,
    Missing,
    Error,
  };

  /// @brief Brings up `label` as an NVS partition. A partition with no free page is damaged or resized and is erased
  ///        and reformatted. One written in a newer NVS format is only erased when `eraseNewer`: the shared `nvs`
  ///        regenerates everything in it, but `cfg` in a newer format is a rollback, and the firmware rolled forward
  ///        to must still find it. False when the partition cannot be used.
  [[nodiscard]] bool InitPartition(const char* label, bool eraseNewer);

  class Handle {
  public:
    /// @brief Opens `ns` in `partition`. A read-only open of a namespace that does not exist yet fails with nullopt
    ///        and no error logged; a load cannot tell that from a failed open, so a load uses OpenForLoad instead.
    [[nodiscard]] static std::optional<Handle> Open(const char* partition, const char* ns, bool write);

    /// @brief Opens `ns` in `partition` read-only for a load. A namespace that was never written is an empty handle
    ///        (Exists() false) on which every read is Missing: every key is its default. Nullopt only when the open
    ///        failed, and the load must fail with it, since defaults would be taken for what is on flash.
    [[nodiscard]] static std::optional<Handle> OpenForLoad(const char* partition, const char* ns);

    Handle(Handle&& other) noexcept;
    Handle& operator=(Handle&& other) noexcept;
    Handle(const Handle&)            = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle();

    /// @brief False for the empty handle OpenForLoad gives a namespace that was never written.
    [[nodiscard]] bool Exists() const { return m_handle != 0; }
    /// @brief The raw handle, for the untyped key copy in Nvs.cpp; sections never need it.
    [[nodiscard]] nvs_handle_t get() const { return m_handle; }

    [[nodiscard]] Result GetU8(const char* key, uint8_t& out) const;
    [[nodiscard]] Result GetI8(const char* key, int8_t& out) const;
    [[nodiscard]] Result GetU16(const char* key, uint16_t& out) const;
    [[nodiscard]] Result GetI32(const char* key, int32_t& out) const;
    [[nodiscard]] Result GetStr(const char* key, std::string& out) const;
    [[nodiscard]] Result GetBlob(const char* key, std::vector<uint8_t>& out) const;

    [[nodiscard]] bool SetU8(const char* key, uint8_t value);
    [[nodiscard]] bool SetBool(const char* key, bool value);
    [[nodiscard]] bool SetI8(const char* key, int8_t value);
    [[nodiscard]] bool SetU16(const char* key, uint16_t value);
    [[nodiscard]] bool SetI32(const char* key, int32_t value);
    [[nodiscard]] bool SetStr(const char* key, std::string_view value);
    [[nodiscard]] bool SetBlob(const char* key, std::span<const uint8_t> value);

    /// @brief Removes `key`; a key that is not there counts as removed.
    [[nodiscard]] bool Erase(const char* key);
    [[nodiscard]] bool EraseAll();
    [[nodiscard]] bool Commit();

    /// @brief Calls `fn(key)` for every key of `type` (NVS_TYPE_ANY for all) in the namespace.
    template<typename Fn>
    bool ForEachKey(nvs_type_t type, Fn&& fn) const
    {
      if (!Exists()) {
        return true;
      }
      nvs_iterator_t it = nullptr;
      esp_err_t err     = nvs_entry_find_in_handle(m_handle, type, &it);
      while (err == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        fn(info.key);
        err = nvs_entry_next(&it);
      }
      nvs_release_iterator(it);
      return err == ESP_ERR_NVS_NOT_FOUND;  // the iterator ran off the end, which is the only clean exit
    }

  private:
    explicit Handle(nvs_handle_t handle)
      : m_handle(handle)
    {
    }

    nvs_handle_t m_handle;
  };

  // Loads a bool / integer / string with its default: Missing takes `defaultValue`, Error returns false.
  bool Load(const Handle& handle, const char* key, bool& out, bool defaultValue);
  bool Load(const Handle& handle, const char* key, uint8_t& out, uint8_t defaultValue);
  bool Load(const Handle& handle, const char* key, int8_t& out, int8_t defaultValue);
  bool Load(const Handle& handle, const char* key, uint16_t& out, uint16_t defaultValue);
  bool Load(const Handle& handle, const char* key, int32_t& out, int32_t defaultValue);
  bool Load(const Handle& handle, const char* key, std::string& out, std::string_view defaultValue);

  /// @brief Writes a section value's keys, member by member, skipping those equal to `previous` (the value on flash
  ///        as the caller last saw it; null when unknown, so every key is written). A member stored under another
  ///        type (an enum as u8, a gpio_num_t as i8) names it: `Set<uint8_t>(key, &T::channel)`. One failed write
  ///        fails the chain; Commit() reports it.
  template<typename T>
  class Saver {
  public:
    Saver(Handle& handle, const T& value, const T* previous)
      : m_handle(handle)
      , m_value(value)
      , m_previous(previous)
      , m_ok(true)
    {
    }

    template<typename S = void, typename M>
    Saver& Set(const char* key, M T::* member)
    {
      if (!m_ok) {
        return *this;
      }
      if (m_previous != nullptr && m_previous->*member == m_value.*member) {
        return *this;
      }
      if constexpr (std::is_void_v<S>) {
        m_ok = (m_handle.*pick<M>())(key, m_value.*member);
      } else {
        m_ok = (m_handle.*pick<S>())(key, static_cast<S>(m_value.*member));
      }
      return *this;
    }

    [[nodiscard]] bool Commit() { return m_ok && m_handle.Commit(); }

  private:
    template<typename S>
    static constexpr auto pick()
    {
      if constexpr (std::is_same_v<S, bool>) {
        return &Handle::SetBool;
      } else if constexpr (std::is_same_v<S, uint8_t>) {
        return &Handle::SetU8;
      } else if constexpr (std::is_same_v<S, int8_t>) {
        return &Handle::SetI8;
      } else if constexpr (std::is_same_v<S, uint16_t>) {
        return &Handle::SetU16;
      } else if constexpr (std::is_same_v<S, int32_t>) {
        return &Handle::SetI32;
      } else {
        static_assert(std::is_same_v<S, std::string>, "no NVS type for this member; name the stored type: Set<uint8_t>(key, &T::member)");
        return &Handle::SetStr;
      }
    }

    Handle& m_handle;
    const T& m_value;
    const T* m_previous;
    bool m_ok;
  };

  /// @brief Erases every key of `ns` in `partition`; a namespace that does not exist counts as erased.
  [[nodiscard]] bool EraseNamespace(const char* partition, const char* ns);

  /// @brief Copies every key of `ns` from partition `from` into partition `to`, whatever its type, and commits.
  [[nodiscard]] bool CopyNamespace(const char* from, const char* to, const char* ns);

  /// @brief Whether `ns` holds the same keys with the same types and values in both partitions.
  [[nodiscard]] bool NamespacesEqual(const char* a, const char* b, const char* ns);
}  // namespace OpenShock::Config::Nvs
