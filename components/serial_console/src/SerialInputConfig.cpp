#include <freertos/FreeRTOS.h>

#include "serial_console/SerialInputConfig.h"

const char* const TAG = "Config::SerialInputConfig";

#include "config/Nvs.h"
#include "config/Parse.h"
#include "Logging.h"

#include "json/Json.h"

using namespace OpenShock::Config;
namespace Fbs = OpenShock::Serialization::Configuration;

static constexpr const char* kNamespace = "serial";
static constexpr const char* kKeyEcho   = "echo";

static constexpr const char* kNamespaces[] = {kNamespace};
static Section<SerialInputConfig> s_section({.name = "serialInput", .order = 4, .hubConfigField = Fbs::HubConfig::VT_SERIAL_INPUT, .namespaces = kNamespaces});

SerialInputConfig::SerialInputConfig()
  : echoEnabled(true)
{
}

SerialInputConfig::SerialInputConfig(bool echoEnabled)
  : echoEnabled(echoEnabled)
{
}

flatbuffers::Offset<OpenShock::Serialization::Configuration::SerialInputConfig> SerialInputConfig::ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const
{
  return Serialization::Configuration::CreateSerialInputConfig(builder, echoEnabled);
}

bool SerialInputConfig::FromJSON(JSON::JsonView json)
{
  if (auto done = Parse::ObjectOrDefault(*this, json, TAG)) {
    return *done;
  }

  if (!Parse::JsonBool(echoEnabled, json, "echoEnabled", true, TAG)) {
    return false;
  }

  return true;
}

void SerialInputConfig::ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const
{
  JSON::objBegin(gen, name);
  json_gen_obj_set_bool(gen, "echoEnabled", echoEnabled);
  JSON::objEnd(gen, name);
}

bool SerialInputConfig::LoadNvs(const char* partition)
{
  auto handle = Nvs::Handle::OpenForLoad(partition, kNamespace);
  if (!handle) {
    return false;
  }
  return Nvs::Load(*handle, kKeyEcho, echoEnabled, true);
}

bool SerialInputConfig::SaveNvs(const char* partition, const SerialInputConfig* previous) const
{
  auto handle = Nvs::Handle::Open(partition, kNamespace, true);
  if (!handle) {
    return false;
  }
  return Nvs::Saver(*handle, *this, previous).Set(kKeyEcho, &SerialInputConfig::echoEnabled).Commit();
}

SerialInputConfig OpenShock::Config::GetSerialInputConfig()
{
  return s_section.Get();
}

bool OpenShock::Config::SetSerialInputConfig(const SerialInputConfig& config)
{
  return s_section.Set(config);
}

bool OpenShock::Config::GetSerialInputConfigEchoEnabled()
{
  return s_section.Read([](const SerialInputConfig& serial) { return serial.echoEnabled; });
}

bool OpenShock::Config::SetSerialInputConfigEchoEnabled(bool enabled)
{
  return s_section.Update([&](SerialInputConfig& serial) {
    serial.echoEnabled = enabled;
    return true;
  });
}
