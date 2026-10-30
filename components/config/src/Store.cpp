#include <freertos/FreeRTOS.h>

#include "config/Store.h"

const char* const TAG = "Config";

#include "config/Nvs.h"
#include "config/Section.h"
#include "config/Wire.h"
#include "Logging.h"
#include "SimpleMutex.h"

#include "fs/LfsPartition.h"
#include "json/Json.h"

#include <esp_partition.h>
#include <nvs_flash.h>

#include <cstring>
#include <string>
#include <vector>

using namespace OpenShock;

static constexpr const char* kPartitionCfg    = "cfg";
static constexpr const char* kPartitionShared = "nvs";
static constexpr const char* kRootNamespace   = "root";
static constexpr const char* kSchemaKey       = "schema";
static constexpr uint8_t kSchemaVersion       = 1;

// The 1.x config file: the HubConfig the Arduino firmware stored as "config" at the root of the littlefs partition
// labelled "config" (partitions/legacy/ota_4mb_v1.csv). Read once, never written, so a rollback to 1.x finds it.
static constexpr const char* kLegacyPartitionLabel = "config";
static constexpr const char* kLegacyFilePath       = "/config";

// Written by Init() alone, under the write lock, before anything else reads a section.
static const char* s_partition = nullptr;

const char* Config::Internal::Partition()
{
  return s_partition;
}

const char* Config::StorageLabel()
{
  return s_partition;
}

#define CONFIG_LOCK_WRITE() ScopedLock lock__(&Config::Internal::WriteLock())

// A store marked by a newer firmware (a rollback) counts as filled: it is loaded best-effort, never refilled.
static bool hasSchema(const char* partition)
{
  auto handle = Config::Nvs::Handle::Open(partition, kRootNamespace, false);
  if (!handle) {
    return false;
  }
  uint8_t schema = 0;
  if (handle->GetU8(kSchemaKey, schema) != Config::Nvs::Result::Found || schema < kSchemaVersion) {
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

// Every namespace the config owns: the store's own and each section's.
static std::vector<const char*> allNamespaces()
{
  std::vector<const char*> namespaces {kRootNamespace};
  for (auto* section = Config::SectionBase::First(); section != nullptr; section = section->Next()) {
    for (const char* ns : section->Info().namespaces) {
      namespaces.push_back(ns);
    }
  }
  return namespaces;
}

// Brings up `label` as an NVS partition. A partition with no free page is damaged or resized and is erased and
// reformatted. One written in a newer NVS format is only erased when `eraseNewer`: the shared `nvs` regenerates
// everything in it, but `cfg` in a newer format is a rollback, and the firmware rolled forward to must still find it.
static bool initNvsPartition(const char* label, bool eraseNewer)
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

static bool nvsPartitionExists(const char* label)
{
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, label) != nullptr;
}

// The 2.x cfg is the 1.x littlefs `config` partition retyped in place, so on the first boot after the table rewrite it
// still holds that littlefs. NVS must never try to make sense of it: it is erased. littlefs names itself in the
// superblock entry at the start of either of its first two blocks (revision count, tag, then "littlefs").
static void clearLegacyLittlefs(const char* label)
{
  const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, label);
  if (partition == nullptr) {
    return;
  }
  static constexpr uint32_t kBlockSize   = 0x1000;
  static constexpr uint32_t kMagicOffset = 8;
  static constexpr char kMagic[]         = {'l', 'i', 't', 't', 'l', 'e', 'f', 's'};
  for (uint32_t block = 0; block < 2 && (block + 1) * kBlockSize <= partition->size; ++block) {
    char magic[sizeof(kMagic)];
    if (esp_partition_read(partition, block * kBlockSize + kMagicOffset, magic, sizeof(magic)) != ESP_OK || memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
      continue;
    }
    OS_LOGI(TAG, "Partition %s still holds the 1.x littlefs; erasing it", label);
    if (esp_partition_erase_range(partition, 0, partition->size) != ESP_OK) {
      OS_LOGE(TAG, "Failed to erase partition %s", label);
    }
    return;
  }
}

static const char* openStorage()
{
  if (nvsPartitionExists(kPartitionCfg)) {
    clearLegacyLittlefs(kPartitionCfg);
    if (initNvsPartition(kPartitionCfg, false)) {
      return kPartitionCfg;
    }
    // Never the shared nvs instead: what is saved there is discarded once cfg opens again
    OS_LOGE(TAG, "The cfg partition is unusable; running on defaults in RAM, nothing will be saved");
    return nullptr;
  }
  if (nvsPartitionExists(kPartitionShared) && initNvsPartition(kPartitionShared, true)) {
    OS_LOGW(TAG, "No cfg partition (1.x partition layout): the config is kept in the shared nvs partition until the layout is migrated");
    return kPartitionShared;
  }
  OS_LOGE(TAG, "No NVS partition for the config; running on defaults in RAM, nothing will be saved");
  return nullptr;
}

// First boot on a 2.x layout after the table rewrite: what the 1.9.9 pivot kept in the shared `nvs` is copied into
// `cfg` key by key, compared, and only then deleted from the shared partition. The schema marker (`root`) is copied
// last, so a power cut mid-copy leaves `cfg` unmarked and the copy is redone. Idempotent: once `cfg` has a schema the
// shared copy is only ever cleaned up. Returns the partition the config now lives in: `cfg`, or the shared `nvs` when
// the copy failed.
static const char* moveSharedIntoCfg()
{
  if (!nvsPartitionExists(kPartitionShared) || !initNvsPartition(kPartitionShared, true) || !hasSchema(kPartitionShared)) {
    return kPartitionCfg;
  }
  const auto namespaces = allNamespaces();  // kRootNamespace first, so walked backwards

  if (!hasSchema(kPartitionCfg)) {
    bool ok = true;
    for (std::size_t i = namespaces.size(); ok && i-- > 0;) {
      ok = Config::Nvs::CopyNamespace(kPartitionShared, kPartitionCfg, namespaces[i]) && Config::Nvs::NamespacesEqual(kPartitionShared, kPartitionCfg, namespaces[i]);
    }
    if (!ok) {
      OS_LOGE(TAG, "Failed to copy the config into cfg; it stays in the shared nvs partition");
      for (const char* ns : namespaces) {
        (void)Config::Nvs::EraseNamespace(kPartitionCfg, ns);
      }
      return kPartitionShared;
    }
    OS_LOGI(TAG, "Moved the config from the shared nvs partition into cfg");
  }

  bool erased = true;
  for (const char* ns : namespaces) {
    erased &= Config::Nvs::EraseNamespace(kPartitionShared, ns);
  }
  if (erased) {
    OS_LOGI(TAG, "Removed the config copy from the shared nvs partition");
  }
  return kPartitionCfg;
}

// The config file a 1.x hub left behind, as a jsonconfig document. Nullopt when the hub has none.
static std::optional<std::string> readLegacyConfig()
{
  const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, kLegacyPartitionLabel);
  if (partition == nullptr) {
    return std::nullopt;  // Not a 1.x layout: nothing to import
  }

  // Read-only and never formatted: the file must survive a rollback to 1.x
  LfsPartition fs;
  if (!fs.mount(partition, /*writable=*/false, /*formatOnFail=*/false)) {
    OS_LOGW(TAG, "The 1.x config partition does not mount; nothing to import");
    return std::nullopt;
  }
  std::vector<uint8_t> blob;
  bool read = fs.readAll(kLegacyFilePath, blob);
  fs.unmount();
  if (!read) {
    OS_LOGW(TAG, "The 1.x config partition holds no config file; nothing to import");
    return std::nullopt;
  }

  auto json = Config::HubConfigToJSON(blob);
  if (!json) {
    OS_LOGE(TAG, "The 1.x config file (%zu bytes) is not a valid config; nothing to import", blob.size());
    return std::nullopt;
  }
  OS_LOGI(TAG, "Importing the 1.x config (%zu bytes)", blob.size());
  return json;
}

// Requires the write lock. Stages `root` in every section; on success commits each (every key when `full`).
static bool applyDocument(JSON::JsonView root, bool full)
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
    if (!section->CommitStaged(s_partition, full)) {
      OS_LOGE(TAG, "Failed to save the %s config", section->Info().name);
      ok = false;
    }
  }
  return ok;
}

// Requires the write lock. Fills an unmarked store: the 1.x file if there is one, else the defaults.
static void fillStore()
{
  bool filled = false;
  if (auto legacy = readLegacyConfig()) {
    JSON::JsonDocument doc;
    if (doc.parse(*legacy) && applyDocument(doc.root(), true)) {
      filled = true;
    } else {
      OS_LOGE(TAG, "Failed to apply the imported 1.x config; writing the defaults");
    }
  }
  if (!filled) {
    OS_LOGW(TAG, "No stored config, writing the defaults");
    for (auto* section = Config::SectionBase::First(); section != nullptr; section = section->Next()) {
      section->Reset(s_partition);
    }
  }

  if (s_partition == nullptr) {
    return;
  }
  // Read back: what the hub runs with must be what is on flash, as every later save assumes
  for (auto* section = Config::SectionBase::First(); section != nullptr; section = section->Next()) {
    if (!section->VerifyStored(s_partition)) {
      OS_LOGE(TAG, "The %s config does not read back as written to %s", section->Info().name, s_partition);
    }
  }
  if (!writeSchema(s_partition)) {
    OS_LOGE(TAG, "Failed to mark the config store; it will be filled again on the next boot");
  }
}

void Config::Init()
{
  CONFIG_LOCK_WRITE();

  s_partition = openStorage();
  if (s_partition != nullptr && std::strcmp(s_partition, kPartitionCfg) == 0) {
    s_partition = moveSharedIntoCfg();
  }

  if (s_partition == nullptr || !hasSchema(s_partition)) {
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
  return applyDocument(doc.root(), false);
}
