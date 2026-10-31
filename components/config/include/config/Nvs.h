#pragma once

// Typed access to one NVS namespace: what every module's config section uses for its LoadNvs / SaveNvs. The
// partition is the one the store picked (Config::StorageLabel(), handed to LoadNvs/SaveNvs), never hardcoded.
//
// Reads are tri-state (Found / Missing / Error): a missing key is a default, an error fails the load. Writes
// take the previous value so a save touches only keys whose value changed: NVS is append-only with
// copy-on-write pages, so every write costs an entry until the page is compacted, and a field that did not
// change costs nothing. NVS guarantees atomicity per key, not across keys: anything that must change together
// belongs in one blob.
//
// Namespace and key names are at most 15 characters and are effectively permanent once devices hold them.

#include <nvs.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace OpenShock::Config::Nvs {
  enum class Result : uint8_t {
    Found,
    Missing,
    Error,
  };

  class Handle {
  public:
    /// @brief Opens `ns` in `partition`. Read-only opens of a namespace that does not exist yet fail with nullopt
    ///        and no error logged: that is what a fresh store looks like, and every read then yields Missing.
    ///        A load must not use this: it cannot tell that case from a failed open (use OpenRead).
    [[nodiscard]] static std::optional<Handle> Open(const char* partition, const char* ns, bool write);

    /// @brief Opens `ns` in `partition` read-only for a load. Found: `out` holds the handle. Missing: the namespace
    ///        was never written, `out` is empty and every key is its default. Error: the open failed and the load
    ///        must fail with it, since defaults would be taken for what is on flash.
    [[nodiscard]] static Result OpenRead(const char* partition, const char* ns, std::optional<Handle>& out);

    Handle(Handle&& other) noexcept;
    Handle& operator=(Handle&& other) noexcept;
    Handle(const Handle&)            = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle();

    [[nodiscard]] nvs_handle_t get() const { return m_handle; }

    [[nodiscard]] Result GetU8(const char* key, uint8_t& out) const;
    [[nodiscard]] Result GetI8(const char* key, int8_t& out) const;
    [[nodiscard]] Result GetU16(const char* key, uint16_t& out) const;
    [[nodiscard]] Result GetI32(const char* key, int32_t& out) const;
    [[nodiscard]] Result GetStr(const char* key, std::string& out) const;
    [[nodiscard]] Result GetBlob(const char* key, std::vector<uint8_t>& out) const;

    // `previous` is the value on flash as the caller last saw it (null when unknown): the write is skipped when
    // it equals `value`.
    [[nodiscard]] bool SetU8(const char* key, uint8_t value, const uint8_t* previous = nullptr);
    [[nodiscard]] bool SetBool(const char* key, bool value, const bool* previous = nullptr);
    [[nodiscard]] bool SetI8(const char* key, int8_t value, const int8_t* previous = nullptr);
    [[nodiscard]] bool SetU16(const char* key, uint16_t value, const uint16_t* previous = nullptr);
    [[nodiscard]] bool SetI32(const char* key, int32_t value, const int32_t* previous = nullptr);
    [[nodiscard]] bool SetStr(const char* key, std::string_view value, const std::string* previous = nullptr);
    [[nodiscard]] bool SetBlob(const char* key, std::span<const uint8_t> value);

    /// @brief Removes `key`; a key that is not there counts as removed.
    [[nodiscard]] bool Erase(const char* key);
    [[nodiscard]] bool EraseAll();
    [[nodiscard]] bool Commit();

    /// @brief Calls `fn(key)` for every key of `type` (NVS_TYPE_ANY for all) in the namespace.
    template<typename Fn>
    bool ForEachKey(nvs_type_t type, Fn&& fn) const
    {
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

  /// @brief Erases every key of `ns` in `partition`; a namespace that does not exist counts as erased.
  [[nodiscard]] bool EraseNamespace(const char* partition, const char* ns);

  /// @brief Copies every key of `ns` from partition `from` into partition `to`, whatever its type, and commits.
  [[nodiscard]] bool CopyNamespace(const char* from, const char* to, const char* ns);

  /// @brief Whether `ns` holds the same keys with the same types and values in both partitions.
  [[nodiscard]] bool NamespacesEqual(const char* a, const char* b, const char* ns);
}  // namespace OpenShock::Config::Nvs
