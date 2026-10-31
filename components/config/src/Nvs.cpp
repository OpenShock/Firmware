#include "config/Nvs.h"

const char* const TAG = "Config::Nvs";

#include "Logging.h"

#include <nvs_flash.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using namespace OpenShock::Config;

bool Nvs::InitPartition(const char* label, bool eraseNewer)
{
  esp_err_t err = nvs_flash_init_partition(label);
  if (err == ESP_ERR_NVS_NEW_VERSION_FOUND && !eraseNewer) {
    OS_LOGE(TAG, "NVS partition %s was written by a newer firmware; leaving it untouched", label);
    return false;
  }
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    OS_LOGW(TAG, "NVS partition %s unusable (%s), erasing and reformatting", label, esp_err_to_name(err));
    if (nvs_flash_erase_partition(label) != ESP_OK) {
      OS_LOGE(TAG, "Failed to erase NVS partition %s", label);
      return false;
    }
    err = nvs_flash_init_partition(label);
  }
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to initialize NVS partition %s: %s", label, esp_err_to_name(err));
    return false;
  }
  return true;
}

std::optional<Nvs::Handle> Nvs::Handle::Open(const char* partition, const char* ns, bool write)
{
  nvs_handle_t handle = 0;
  esp_err_t err       = nvs_open_from_partition(partition, ns, write ? NVS_READWRITE : NVS_READONLY, &handle);
  if (err == ESP_OK) {
    return Handle(handle);
  }
  if (err == ESP_ERR_NVS_NOT_FOUND && !write) {
    return std::nullopt;  // The namespace was never written: every key is a default
  }
  OS_LOGE(TAG, "Failed to open %s/%s: %s", partition, ns, esp_err_to_name(err));
  return std::nullopt;
}

std::optional<Nvs::Handle> Nvs::Handle::OpenForLoad(const char* partition, const char* ns)
{
  nvs_handle_t handle = 0;
  esp_err_t err       = nvs_open_from_partition(partition, ns, NVS_READONLY, &handle);
  if (err == ESP_OK) {
    return Handle(handle);
  }
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    return Handle(0);  // The namespace was never written: every key is a default
  }
  OS_LOGE(TAG, "Failed to open %s/%s: %s", partition, ns, esp_err_to_name(err));
  return std::nullopt;
}

Nvs::Handle::Handle(Handle&& other) noexcept
  : m_handle(std::exchange(other.m_handle, 0))
{
}

Nvs::Handle& Nvs::Handle::operator=(Handle&& other) noexcept
{
  if (this != &other) {
    if (m_handle != 0) {
      nvs_close(m_handle);
    }
    m_handle = std::exchange(other.m_handle, 0);
  }
  return *this;
}

Nvs::Handle::~Handle()
{
  if (m_handle != 0) {
    nvs_close(m_handle);
  }
}

namespace {
  Nvs::Result toResult(esp_err_t err, const char* key)
  {
    if (err == ESP_OK) {
      return Nvs::Result::Found;
    }
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      return Nvs::Result::Missing;
    }
    OS_LOGE(TAG, "Failed to read %s: %s", key, esp_err_to_name(err));
    return Nvs::Result::Error;
  }

  bool checkWrite(esp_err_t err, const char* key)
  {
    if (err != ESP_OK) {
      OS_LOGE(TAG, "Failed to write %s: %s", key, esp_err_to_name(err));
      return false;
    }
    return true;
  }
}  // namespace

Nvs::Result Nvs::Handle::GetU8(const char* key, uint8_t& out) const
{
  if (!Exists()) return Result::Missing;
  return toResult(nvs_get_u8(m_handle, key, &out), key);
}
Nvs::Result Nvs::Handle::GetI8(const char* key, int8_t& out) const
{
  if (!Exists()) return Result::Missing;
  return toResult(nvs_get_i8(m_handle, key, &out), key);
}
Nvs::Result Nvs::Handle::GetU16(const char* key, uint16_t& out) const
{
  if (!Exists()) return Result::Missing;
  return toResult(nvs_get_u16(m_handle, key, &out), key);
}
Nvs::Result Nvs::Handle::GetI32(const char* key, int32_t& out) const
{
  if (!Exists()) return Result::Missing;
  return toResult(nvs_get_i32(m_handle, key, &out), key);
}

Nvs::Result Nvs::Handle::GetStr(const char* key, std::string& out) const
{
  if (!Exists()) return Result::Missing;
  size_t length = 0;
  Result result = toResult(nvs_get_str(m_handle, key, nullptr, &length), key);
  if (result != Result::Found) {
    return result;
  }
  std::string value(length, '\0');  // length includes the NUL
  result = toResult(nvs_get_str(m_handle, key, value.data(), &length), key);
  if (result != Result::Found) {
    return result;
  }
  value.resize(length > 0 ? length - 1 : 0);
  out = std::move(value);
  return Result::Found;
}

Nvs::Result Nvs::Handle::GetBlob(const char* key, std::vector<uint8_t>& out) const
{
  if (!Exists()) return Result::Missing;
  size_t length = 0;
  Result result = toResult(nvs_get_blob(m_handle, key, nullptr, &length), key);
  if (result != Result::Found) {
    return result;
  }
  std::vector<uint8_t> value(length);
  if (length > 0) {
    result = toResult(nvs_get_blob(m_handle, key, value.data(), &length), key);
    if (result != Result::Found) {
      return result;
    }
  }
  out = std::move(value);
  return Result::Found;
}

bool Nvs::Handle::SetU8(const char* key, uint8_t value)
{
  return checkWrite(nvs_set_u8(m_handle, key, value), key);
}
bool Nvs::Handle::SetBool(const char* key, bool value)
{
  return checkWrite(nvs_set_u8(m_handle, key, value ? 1 : 0), key);
}
bool Nvs::Handle::SetI8(const char* key, int8_t value)
{
  return checkWrite(nvs_set_i8(m_handle, key, value), key);
}
bool Nvs::Handle::SetU16(const char* key, uint16_t value)
{
  return checkWrite(nvs_set_u16(m_handle, key, value), key);
}
bool Nvs::Handle::SetI32(const char* key, int32_t value)
{
  return checkWrite(nvs_set_i32(m_handle, key, value), key);
}
bool Nvs::Handle::SetStr(const char* key, std::string_view value)
{
  // nvs_set_str wants a C string: a value with an embedded NUL would be silently cut, so refuse it
  if (value.find('\0') != std::string_view::npos) {
    OS_LOGE(TAG, "Refusing to store %s: embedded NUL", key);
    return false;
  }
  std::string copy(value);
  return checkWrite(nvs_set_str(m_handle, key, copy.c_str()), key);
}
bool Nvs::Handle::SetBlob(const char* key, std::span<const uint8_t> value)
{
  return checkWrite(nvs_set_blob(m_handle, key, value.data(), value.size()), key);
}

bool Nvs::Handle::Erase(const char* key)
{
  esp_err_t err = nvs_erase_key(m_handle, key);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    return true;
  }
  return checkWrite(err, key);
}
bool Nvs::Handle::EraseAll()
{
  return checkWrite(nvs_erase_all(m_handle), "*");
}
bool Nvs::Handle::Commit()
{
  return checkWrite(nvs_commit(m_handle), "commit");
}

namespace {
  template<typename T, typename Getter>
  bool loadWith(Getter getter, const char* key, T& out, T defaultValue)
  {
    T value {};
    switch (getter(key, value)) {
      case Nvs::Result::Found:
        out = value;
        return true;
      case Nvs::Result::Missing:
        out = defaultValue;
        return true;
      case Nvs::Result::Error:
        return false;
    }
    return false;
  }
}  // namespace

bool Nvs::Load(const Handle& handle, const char* key, bool& out, bool defaultValue)
{
  uint8_t value = 0;
  switch (handle.GetU8(key, value)) {
    case Result::Found:
      out = value != 0;
      return true;
    case Result::Missing:
      out = defaultValue;
      return true;
    case Result::Error:
      return false;
  }
  return false;
}
bool Nvs::Load(const Handle& handle, const char* key, uint8_t& out, uint8_t defaultValue)
{
  return loadWith<uint8_t>([&](const char* k, uint8_t& v) { return handle.GetU8(k, v); }, key, out, defaultValue);
}
bool Nvs::Load(const Handle& handle, const char* key, int8_t& out, int8_t defaultValue)
{
  return loadWith<int8_t>([&](const char* k, int8_t& v) { return handle.GetI8(k, v); }, key, out, defaultValue);
}
bool Nvs::Load(const Handle& handle, const char* key, uint16_t& out, uint16_t defaultValue)
{
  return loadWith<uint16_t>([&](const char* k, uint16_t& v) { return handle.GetU16(k, v); }, key, out, defaultValue);
}
bool Nvs::Load(const Handle& handle, const char* key, int32_t& out, int32_t defaultValue)
{
  return loadWith<int32_t>([&](const char* k, int32_t& v) { return handle.GetI32(k, v); }, key, out, defaultValue);
}
bool Nvs::Load(const Handle& handle, const char* key, std::string& out, std::string_view defaultValue)
{
  std::string value;
  switch (handle.GetStr(key, value)) {
    case Result::Found:
      out = std::move(value);
      return true;
    case Result::Missing:
      out = defaultValue;
      return true;
    case Result::Error:
      return false;
  }
  return false;
}

bool Nvs::EraseNamespace(const char* partition, const char* ns)
{
  // Looked up read-only first: a read-write open creates the namespace, which one that is not there must not do
  auto existing = Handle::OpenForLoad(partition, ns);
  if (!existing) {
    return false;
  }
  if (!existing->Exists()) {
    return true;
  }
  existing.reset();
  auto handle = Handle::Open(partition, ns, true);
  return handle && handle->EraseAll() && handle->Commit();
}

namespace {
  // One entry of any type, read into a byte image so two can be compared and one can be written back.
  struct RawEntry {
    nvs_type_t type;
    std::vector<uint8_t> data;  // integers little-endian, strings with their NUL, blobs as they are
  };

  bool readRaw(nvs_handle_t handle, const char* key, nvs_type_t type, RawEntry& out)
  {
    out.type      = type;
    esp_err_t err = ESP_OK;
    switch (type) {
      case NVS_TYPE_U8:
      case NVS_TYPE_I8:
        out.data.resize(1);
        err = type == NVS_TYPE_U8 ? nvs_get_u8(handle, key, out.data.data()) : nvs_get_i8(handle, key, reinterpret_cast<int8_t*>(out.data.data()));
        break;
      case NVS_TYPE_U16:
      case NVS_TYPE_I16:
        out.data.resize(2);
        err = type == NVS_TYPE_U16 ? nvs_get_u16(handle, key, reinterpret_cast<uint16_t*>(out.data.data())) : nvs_get_i16(handle, key, reinterpret_cast<int16_t*>(out.data.data()));
        break;
      case NVS_TYPE_U32:
      case NVS_TYPE_I32:
        out.data.resize(4);
        err = type == NVS_TYPE_U32 ? nvs_get_u32(handle, key, reinterpret_cast<uint32_t*>(out.data.data())) : nvs_get_i32(handle, key, reinterpret_cast<int32_t*>(out.data.data()));
        break;
      case NVS_TYPE_U64:
      case NVS_TYPE_I64:
        out.data.resize(8);
        err = type == NVS_TYPE_U64 ? nvs_get_u64(handle, key, reinterpret_cast<uint64_t*>(out.data.data())) : nvs_get_i64(handle, key, reinterpret_cast<int64_t*>(out.data.data()));
        break;
      case NVS_TYPE_STR:
      {
        size_t length = 0;
        err           = nvs_get_str(handle, key, nullptr, &length);
        if (err == ESP_OK) {
          out.data.resize(length);
          err = nvs_get_str(handle, key, reinterpret_cast<char*>(out.data.data()), &length);
        }
        break;
      }
      case NVS_TYPE_BLOB:
      {
        size_t length = 0;
        err           = nvs_get_blob(handle, key, nullptr, &length);
        if (err == ESP_OK) {
          out.data.resize(length);
          err = length == 0 ? ESP_OK : nvs_get_blob(handle, key, out.data.data(), &length);
        }
        break;
      }
      default:
        OS_LOGE(TAG, "Key %s has an unknown type 0x%02x", key, type);
        return false;
    }
    if (err != ESP_OK) {
      OS_LOGE(TAG, "Failed to read %s: %s", key, esp_err_to_name(err));
      return false;
    }
    return true;
  }

  bool writeRaw(nvs_handle_t handle, const char* key, const RawEntry& entry)
  {
    const void* p = entry.data.data();
    esp_err_t err = ESP_OK;
    switch (entry.type) {
      case NVS_TYPE_U8:
        err = nvs_set_u8(handle, key, *static_cast<const uint8_t*>(p));
        break;
      case NVS_TYPE_I8:
        err = nvs_set_i8(handle, key, *static_cast<const int8_t*>(p));
        break;
      case NVS_TYPE_U16:
        err = nvs_set_u16(handle, key, *static_cast<const uint16_t*>(p));
        break;
      case NVS_TYPE_I16:
        err = nvs_set_i16(handle, key, *static_cast<const int16_t*>(p));
        break;
      case NVS_TYPE_U32:
        err = nvs_set_u32(handle, key, *static_cast<const uint32_t*>(p));
        break;
      case NVS_TYPE_I32:
        err = nvs_set_i32(handle, key, *static_cast<const int32_t*>(p));
        break;
      case NVS_TYPE_U64:
        err = nvs_set_u64(handle, key, *static_cast<const uint64_t*>(p));
        break;
      case NVS_TYPE_I64:
        err = nvs_set_i64(handle, key, *static_cast<const int64_t*>(p));
        break;
      case NVS_TYPE_STR:
        err = nvs_set_str(handle, key, static_cast<const char*>(p));
        break;
      case NVS_TYPE_BLOB:
        err = nvs_set_blob(handle, key, p, entry.data.size());
        break;
      default:
        return false;
    }
    return checkWrite(err, key);
  }

  // Every key of `ns` in `partition` with its type. False when the namespace cannot be enumerated.
  bool listKeys(const char* partition, const char* ns, std::vector<std::pair<std::string, nvs_type_t>>& out)
  {
    out.clear();
    nvs_iterator_t it = nullptr;
    esp_err_t err     = nvs_entry_find(partition, ns, NVS_TYPE_ANY, &it);
    while (err == ESP_OK) {
      nvs_entry_info_t info;
      nvs_entry_info(it, &info);
      out.emplace_back(info.key, info.type);
      err = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    return err == ESP_ERR_NVS_NOT_FOUND;
  }
}  // namespace

bool Nvs::CopyNamespace(const char* from, const char* to, const char* ns)
{
  std::vector<std::pair<std::string, nvs_type_t>> keys;
  if (!listKeys(from, ns, keys)) {
    OS_LOGE(TAG, "Failed to enumerate %s/%s", from, ns);
    return false;
  }
  if (keys.empty()) {
    return true;
  }

  auto src = Handle::Open(from, ns, false);
  auto dst = Handle::Open(to, ns, true);
  if (!src || !dst) {
    return false;
  }
  for (const auto& [key, type] : keys) {
    RawEntry entry;
    if (!readRaw(src->get(), key.c_str(), type, entry) || !writeRaw(dst->get(), key.c_str(), entry)) {
      return false;
    }
  }
  return dst->Commit();
}

bool Nvs::NamespacesEqual(const char* a, const char* b, const char* ns)
{
  std::vector<std::pair<std::string, nvs_type_t>> keysA, keysB;
  if (!listKeys(a, ns, keysA) || !listKeys(b, ns, keysB)) {
    return false;
  }
  std::sort(keysA.begin(), keysA.end());
  std::sort(keysB.begin(), keysB.end());
  if (keysA != keysB) {
    return false;
  }
  if (keysA.empty()) {
    return true;
  }

  auto ha = Handle::Open(a, ns, false);
  auto hb = Handle::Open(b, ns, false);
  if (!ha || !hb) {
    return false;
  }
  for (const auto& [key, type] : keysA) {
    RawEntry ea, eb;
    if (!readRaw(ha->get(), key.c_str(), type, ea) || !readRaw(hb->get(), key.c_str(), type, eb) || ea.data != eb.data) {
      return false;
    }
  }
  return true;
}
