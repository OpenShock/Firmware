#include <freertos/FreeRTOS.h>

#include "config/Store.h"

const char* const TAG = "Config";

#include "config/Nvs.h"
#include "config/Section.h"
#include "Logging.h"
#include "SimpleMutex.h"

#include "json/Json.h"

#include <string>

using namespace OpenShock;

static constexpr const char* kPartitionCfg = "cfg";

// Written by Init() alone, under the write lock, before anything else reads a section.
static const char* s_partition = nullptr;

const char* Config::StorageLabel()
{
  return s_partition;
}

#define CONFIG_LOCK_WRITE() ScopedLock lock__(&Config::Internal::WriteLock())

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
    if (!section->CommitStaged()) {
      OS_LOGE(TAG, "Failed to save the %s config", section->Info().name);
      ok = false;
    }
  }
  return ok;
}

void Config::Init()
{
  CONFIG_LOCK_WRITE();

  s_partition = Config::Nvs::InitPartition(kPartitionCfg, false) ? kPartitionCfg : nullptr;
  if (s_partition == nullptr) {
    OS_LOGE(TAG, "The cfg partition is unusable; running on defaults in RAM, nothing will be saved");
  }

  for (auto* section = SectionBase::First(); section != nullptr; section = section->Next()) {
    if (!section->Load()) {
      // The section runs on its defaults, but flash is left alone: erasing it would take every key with it (all WiFi
      // networks). Its next save rewrites every key.
      OS_LOGE(TAG, "Failed to load the %s config from %s, running it on the defaults", section->Info().name, s_partition);
    }
  }
  if (s_partition != nullptr) {
    OS_LOGI(TAG, "Loaded config from %s", s_partition);
  }
}

void Config::FactoryReset()
{
  CONFIG_LOCK_WRITE();

  bool ok = true;
  for (auto* section = SectionBase::First(); section != nullptr; section = section->Next()) {
    ok &= section->Reset();
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
