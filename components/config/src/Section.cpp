#include <freertos/FreeRTOS.h>

#include "config/Section.h"

#include "config/Nvs.h"

#include "json/Json.h"

using namespace OpenShock;

// Constant-initialized, so it is valid before any section's constructor runs, whatever the static init order.
static constinit Config::SectionBase* s_first = nullptr;

Config::SectionBase::SectionBase(const SectionInfo& info)
  : m_info(info)
  , m_next(nullptr)
{
  // Insert in `order`, so iteration is the jsonconfig document order whatever the link order was
  SectionBase** link = &s_first;
  while (*link != nullptr && (*link)->m_info.order < info.order) {
    link = &(*link)->m_next;
  }
  m_next = *link;
  *link  = this;
}

Config::SectionBase* Config::SectionBase::First()
{
  return s_first;
}

SimpleMutex& Config::Internal::WriteLock()
{
  static SimpleMutex s_writeLock;
  return s_writeLock;
}

bool Config::Internal::EraseNamespaces(const char* partition, std::span<const char* const> namespaces)
{
  bool ok = true;
  for (const char* ns : namespaces) {
    ok &= Nvs::EraseNamespace(partition, ns);
  }
  return ok;
}

bool Config::Internal::SameJson(Util::FunctionRef<void(json_gen_str_t*)> renderA, Util::FunctionRef<void(json_gen_str_t*)> renderB)
{
  JSON::StringWriter a, b;
  json_gen_start_object(a.gen());
  renderA(a.gen());
  json_gen_end_object(a.gen());
  json_gen_start_object(b.gen());
  renderB(b.gen());
  json_gen_end_object(b.gen());
  return a.finish() == b.finish();
}
