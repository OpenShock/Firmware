#include "config/Nvs.h"

const char* const TAG = "Config::Nvs";

#include "Logging.h"

#include <nvs_flash.h>

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

std::optional<Nvs::Handle> Nvs::Handle::OpenForWrite(const char* partition, const char* ns)
{
  nvs_handle_t handle = 0;
  esp_err_t err       = nvs_open_from_partition(partition, ns, NVS_READWRITE, &handle);
  if (err == ESP_OK) {
    return Handle(handle);
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
  auto handle = Handle::OpenForWrite(partition, ns);
  return handle && handle->EraseAll() && handle->Commit();
}
