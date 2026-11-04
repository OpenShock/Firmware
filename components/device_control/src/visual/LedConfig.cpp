#include <freertos/FreeRTOS.h>

#include "visual/LedConfig.h"

const char* const TAG = "Config::LedConfig";

#include "config/Nvs.h"
#include "config/Parse.h"
#include "Logging.h"

#include "json/Json.h"

#include <algorithm>
#include <utility>

using namespace OpenShock::Config;
using OpenShock::LedMode;
using OpenShock::LedSignal;
using OpenShock::LedType;

static constexpr const char* kNamespace    = "led";
static constexpr const char* kKeyMode      = "mode";
static constexpr const char* kKeyMonoCrit  = "monoCrit";
static constexpr const char* kKeyMonoNorm  = "monoNorm";
static constexpr const char* kKeyRgbCrit   = "rgbCrit";
static constexpr const char* kKeyRgbNorm   = "rgbNorm";

// Today's levels: the mono LED at full output, the RGB LED at 20/255 (8%) since a WS2812B at full output is blinding
static constexpr LedMode kDefaultMode       = LedMode::All;
static constexpr uint8_t kDefaultMonoBright = 100;
static constexpr uint8_t kDefaultRgbBright  = 8;

static constexpr const char* kNamespaces[] = {kNamespace};
static Section<LedConfig> s_section({.name = "led", .order = 7, .namespaces = kNamespaces});

LedConfig::LedConfig()
  : mode(kDefaultMode)
  , monoCriticalBrightness(kDefaultMonoBright)
  , monoNormalBrightness(kDefaultMonoBright)
  , rgbCriticalBrightness(kDefaultRgbBright)
  , rgbNormalBrightness(kDefaultRgbBright)
{
}

LedConfig::LedConfig(LedMode mode, uint8_t monoCriticalBrightness, uint8_t monoNormalBrightness, uint8_t rgbCriticalBrightness, uint8_t rgbNormalBrightness)
  : mode(mode)
  , monoCriticalBrightness(monoCriticalBrightness)
  , monoNormalBrightness(monoNormalBrightness)
  , rgbCriticalBrightness(rgbCriticalBrightness)
  , rgbNormalBrightness(rgbNormalBrightness)
{
}

uint8_t& LedConfig::Brightness(LedType type, LedSignal signal)
{
  if (type == LedType::Mono) {
    return signal == LedSignal::Critical ? monoCriticalBrightness : monoNormalBrightness;
  }
  return signal == LedSignal::Critical ? rgbCriticalBrightness : rgbNormalBrightness;
}

void LedConfig::Normalize()
{
  for (uint8_t* brightness : {&monoCriticalBrightness, &monoNormalBrightness, &rgbCriticalBrightness, &rgbNormalBrightness}) {
    *brightness = std::min(*brightness, kMaxBrightness);
  }
}

bool LedConfig::FromJSON(JSON::JsonView json)
{
  if (auto done = Parse::ObjectOrDefault(*this, json, TAG)) {
    return *done;
  }

  // The mode is strict like a boolean: taking the default for a bad value would turn LEDs back on that were turned off
  if (!Parse::JsonStrParsed(mode, json, "mode", OpenShock::TryParseLedMode, kDefaultMode)) {
    OS_LOGE(TAG, "mode is not one of all, critical or off");
    return false;
  }

  monoCriticalBrightness = json["monoCriticalBrightness"].tryGetU8().value_or(kDefaultMonoBright);
  monoNormalBrightness   = json["monoNormalBrightness"].tryGetU8().value_or(kDefaultMonoBright);
  rgbCriticalBrightness  = json["rgbCriticalBrightness"].tryGetU8().value_or(kDefaultRgbBright);
  rgbNormalBrightness    = json["rgbNormalBrightness"].tryGetU8().value_or(kDefaultRgbBright);

  return true;
}

void LedConfig::ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const
{
  (void)withSensitiveData;
  JSON::objBegin(gen, name);
  JSON::objSetString(gen, "mode", OpenShock::LedModeName(mode));
  json_gen_obj_set_int(gen, "monoCriticalBrightness", monoCriticalBrightness);
  json_gen_obj_set_int(gen, "monoNormalBrightness", monoNormalBrightness);
  json_gen_obj_set_int(gen, "rgbCriticalBrightness", rgbCriticalBrightness);
  json_gen_obj_set_int(gen, "rgbNormalBrightness", rgbNormalBrightness);
  JSON::objEnd(gen, name);
}

bool LedConfig::LoadNvs(const char* partition)
{
  auto handle = Nvs::Handle::OpenForLoad(partition, kNamespace);
  if (!handle) {
    return false;
  }

  uint8_t storedMode = std::to_underlying(kDefaultMode);
  bool ok = Nvs::Load(*handle, kKeyMode, storedMode, storedMode) && Nvs::Load(*handle, kKeyMonoCrit, monoCriticalBrightness, kDefaultMonoBright) && Nvs::Load(*handle, kKeyMonoNorm, monoNormalBrightness, kDefaultMonoBright)
         && Nvs::Load(*handle, kKeyRgbCrit, rgbCriticalBrightness, kDefaultRgbBright) && Nvs::Load(*handle, kKeyRgbNorm, rgbNormalBrightness, kDefaultRgbBright);
  if (!ok) {
    return false;
  }

  mode = storedMode <= std::to_underlying(LedMode::Off) ? static_cast<LedMode>(storedMode) : kDefaultMode;
  return true;
}

bool LedConfig::SaveNvs(const char* partition, const LedConfig* previous) const
{
  auto handle = Nvs::Handle::OpenForWrite(partition, kNamespace);
  if (!handle) {
    return false;
  }
  return Nvs::Saver(*handle, *this, previous)
    .Set<uint8_t>(kKeyMode, &LedConfig::mode)
    .Set(kKeyMonoCrit, &LedConfig::monoCriticalBrightness)
    .Set(kKeyMonoNorm, &LedConfig::monoNormalBrightness)
    .Set(kKeyRgbCrit, &LedConfig::rgbCriticalBrightness)
    .Set(kKeyRgbNorm, &LedConfig::rgbNormalBrightness)
    .Commit();
}

LedConfig OpenShock::Config::GetLedConfig()
{
  return s_section.Get();
}

bool OpenShock::Config::SetLedConfig(const LedConfig& config)
{
  return s_section.Set(config);
}

bool OpenShock::Config::SetLedMode(LedMode mode)
{
  return s_section.Update([&](LedConfig& led) {
    led.mode = mode;
    return true;
  });
}

bool OpenShock::Config::SetLedBrightness(LedType type, LedSignal signal, uint8_t percent)
{
  return s_section.Update([&](LedConfig& led) {
    led.Brightness(type, signal) = std::min(percent, LedConfig::kMaxBrightness);
    return true;
  });
}
