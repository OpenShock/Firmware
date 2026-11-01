#include <freertos/FreeRTOS.h>

#include "config/Store.h"

const char* const TAG = "Config";

#include "config/Nvs.h"
#include "config/Section.h"
#include "Logging.h"
#include "SimpleMutex.h"

#include "json/Json.h"

#include <optional>
#include <string>

using namespace OpenShock;

static constexpr const char* kPartitionCfg  = "cfg";
static constexpr const char* kRootNamespace = "root";
static constexpr const char* kSchemaKey     = "schema";
static constexpr uint8_t kSchemaVersion     = 1;

// Written by Init() alone, under the write lock, before anything else reads a section.
static const char* s_partition = nullptr;

const char* Config::StorageLabel()
{
  return s_partition;
}

#define CONFIG_LOCK_WRITE() ScopedLock lock__(&Config::Internal::WriteLock())

// Whether `partition` holds a filled store: true, false, or nullopt when NVS would not say (a failed open or read).
// A store marked by a newer firmware (a rollback) counts as filled: it is loaded best-effort, never refilled.
static std::optional<bool> hasSchema(const char* partition)
{
  auto handle    = Config::Nvs::Handle::OpenForLoad(partition, kRootNamespace);
  uint8_t schema = 0;
  if (!handle || !Config::Nvs::Load(*handle, kSchemaKey, schema, 0)) {
    return std::nullopt;
  }
  if (schema < kSchemaVersion) {
    return false;
  }
  if (schema > kSchemaVersion) {
    OS_LOGW(TAG, "The config in %s has schema %u, newer than this firmware's %u; loading what this firmware knows", partition, schema, kSchemaVersion);
  }
  return true;
}

static bool writeSchema(const char* partition)
{
  auto handle = Config::Nvs::Handle::Open(partition, kRootNamespace, true);
  return handle && handle->SetU8(kSchemaKey, kSchemaVersion) && handle->Commit();
}

// Requires the write lock. Stages `root` in every section; on success commits each.
static bool applyDocument(JSON::JsonView root)
{
  if (!root.isObject()) {
    OS_LOGE(TAG, "The config is not a JSON object");
    return false;
  }

  for (auto* section = Config::SectionBase::First(); section != nullptr; section = section->Next()) {
    if (!section->Stage(root[section->Info().name])) {
      OS_LOGE(TAG, "Unable to read the %s config", section->Info().name);
      for (auto* s = Config::SectionBase::First(); s != nullptr; s = s->Next()) {
        s->DiscardStaged();
      }
      return false;
    }
  }

  bool ok = true;
  for (auto* section = Config::SectionBase::First(); section != nullptr; section = section->Next()) {
    if (!ok) {
      section->DiscardStaged();
      continue;
    }
    if (!section->CommitStaged(s_partition)) {
      OS_LOGE(TAG, "Failed to save the %s config", section->Info().name);
      ok = false;
    }
  }
  return ok;
}

// Requires the write lock. Fills an unmarked store with the defaults. The store is only marked once every section is
// on flash as the hub runs it; a write that failed leaves it unmarked, so the fill is redone on the next boot.
static void fillStore()
{
  OS_LOGW(TAG, "No stored config, writing the defaults");
  bool written = true;
  for (auto* section = Config::SectionBase::First(); section != nullptr; section = section->Next()) {
    written &= section->Reset(s_partition);
  }

  if (s_partition == nullptr) {
    return;
  }
  // Read back: what the hub runs with must be what is on flash, as every later save assumes
  for (auto* section = Config::SectionBase::First(); section != nullptr; section = section->Next()) {
    if (!section->VerifyStored(s_partition)) {
      OS_LOGE(TAG, "The %s config does not read back as written to %s", section->Info().name, s_partition);
      written = false;
    }
  }
  if (!written) {
    OS_LOGE(TAG, "The config store is incomplete; it will be filled again on the next boot");
    return;
  }
  if (!writeSchema(s_partition)) {
    OS_LOGE(TAG, "Failed to mark the config store; it will be filled again on the next boot");
  }
}

void Config::Init()
{
  CONFIG_LOCK_WRITE();

  s_partition = Config::Nvs::InitPartition(kPartitionCfg, false) ? kPartitionCfg : nullptr;
  if (s_partition == nullptr) {
    OS_LOGE(TAG, "The cfg partition is unusable; running on defaults in RAM, nothing will be saved");
  }

  // A store whose marker cannot be read is loaded as if marked: filling it would write the defaults over whatever is
  // there, while a failed load leaves flash alone and runs the section on its defaults
  if (s_partition == nullptr || hasSchema(s_partition) == false) {
    fillStore();
    return;
  }

  for (auto* section = SectionBase::First(); section != nullptr; section = section->Next()) {
    if (!section->Load(s_partition)) {
      // The section runs on its defaults, but flash is left alone: erasing it would take every key with it (all WiFi
      // networks). Its next save rewrites every key.
      OS_LOGE(TAG, "Failed to load the %s config from %s, running it on the defaults", section->Info().name, s_partition);
    }
  }
  OS_LOGI(TAG, "Loaded config from %s", s_partition);
}

void Config::FactoryReset()
{
  CONFIG_LOCK_WRITE();

  bool ok = true;
  for (auto* section = SectionBase::First(); section != nullptr; section = section->Next()) {
    ok &= section->Reset(s_partition);
  }
  if (!ok) {
    OS_PANIC(TAG, "Failed to reset the config. Recommend formatting microcontroller and re-flashing firmware");
  }

  OS_LOGI(TAG, "Factory reset complete");
}

std::string Config::GetAsJSON(bool withSensitiveData)
{
  JSON::StringWriter writer;
  json_gen_str_t* gen = writer.gen();

  json_gen_start_object(gen);
  for (auto* section = SectionBase::First(); section != nullptr; section = section->Next()) {
    section->ToJSON(gen, withSensitiveData);
  }
  json_gen_end_object(gen);

  return writer.finish();
}

bool Config::SaveFromJSON(std::string_view json)
{
  JSON::JsonDocument doc;
  if (!doc.parse(json)) {
    OS_LOGE(TAG, "Failed to parse JSON");
    return false;
  }

  CONFIG_LOCK_WRITE();
  return applyDocument(doc.root());
}
