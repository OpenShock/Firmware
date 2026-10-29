#pragma once

// A config section: one module's settings, owned and defined by that module, kept in NVS through this framework.
//
// The module defines a value type T and one Section<T> object for it (a namespace-scope static in its own .cpp):
//
//   struct RFConfig {
//     void ToDefault();
//     bool LoadNvs(const char* partition);                              // missing keys are defaults
//     bool SaveNvs(const char* partition, const RFConfig* previous) const;  // writes only what differs
//     bool FromJSON(JSON::JsonView json);                               // invalid view: defaults
//     void ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const;
//     flatbuffers::Offset<...> ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const;
//   };
//   static constexpr const char* kNamespaces[] = {"rf"};
//   static Config::Section<RFConfig> s_rf({.name = "rf", .order = 0, .hubConfigField = VT_RF, .namespaces = kNamespaces});
//
// and wraps it in whatever accessors it wants (Config::GetRFConfigTxPin() and so on). Nothing outside the module
// needs to know T's fields or keys; this framework only sees the section through SectionBase.
//
// The section caches its value in RAM: readers copy it out under a short lock and never wait on flash. Writers are
// serialized across every section (one write lock), save what changed, and only then publish the new value, so a
// failed write leaves the running value identical to what is on flash.
//
// Every section links itself into one registry when it is constructed (static initialization), which is how the
// framework loads all of them at boot, renders the whole config as JSON (jsonconfig), imports one, and resets them
// (factory reset). Sections never die; the registry is a constant-initialized intrusive list.

#include "OpenShock.h"
#include "SimpleMutex.h"
#include "util/FunctionRef.h"

#include <json_generator.h>

#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace OpenShock::JSON {
  class JsonView;
}
namespace flatbuffers {
  template<bool Is64Aware>
  class FlatBufferBuilderImpl;
  using FlatBufferBuilder = FlatBufferBuilderImpl<false>;
}  // namespace flatbuffers

namespace OpenShock::Config {
  struct SectionInfo {
    const char* name;                         // member name of the section in the jsonconfig document, e.g. "rf"
    uint8_t order;                            // position in that document and in HubConfig; unique across sections
    uint16_t hubConfigField;                  // the HubConfig field (VT_*) its wire form goes into for the local protocol; 0 = none
    std::span<const char* const> namespaces;  // the NVS namespaces it owns (erased by a reset, moved with the store)
  };

  class SectionBase {
    DISABLE_COPY(SectionBase);
    DISABLE_MOVE(SectionBase);

  public:
    [[nodiscard]] const SectionInfo& Info() const { return m_info; }

    /// @brief The registered sections, in `order`.
    [[nodiscard]] static SectionBase* First();
    [[nodiscard]] SectionBase* Next() const { return m_next; }

    // The interface the store (Store.cpp) drives. All of these run under the write lock.
    virtual bool Load(const char* partition)                                                              = 0;  // from NVS into the cache; false on a read error
    virtual bool Reset(const char* partition)                                                             = 0;  // erase its namespaces, save and publish the defaults
    virtual bool VerifyStored(const char* partition) const                                                = 0;  // whether NVS reads back as the cache
    virtual void ToJSON(json_gen_str_t* gen, bool withSensitiveData) const                                = 0;  // as the member `name`
    virtual bool Stage(const JSON::JsonView& json)                                                        = 0;  // parse into a staging copy
    virtual bool CommitStaged(const char* partition, bool full)                                           = 0;  // save it (every key when full) and publish
    virtual void DiscardStaged()                                                                          = 0;
    virtual uint32_t ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const = 0;  // the sub-table offset

  protected:
    explicit SectionBase(const SectionInfo& info);
    ~SectionBase() = default;  // Sections are statics and are never destroyed

  private:
    const SectionInfo m_info;  // A copy: modules pass a temporary
    SectionBase* m_next;
  };

  namespace Internal {
    /// @brief Serializes every writer of every section, the store's own operations included.
    SimpleMutex& WriteLock();
    /// @brief The partition the store uses ("cfg"; "nvs" on the 1.x layout), or null when the config only lives in RAM.
    const char* Partition();
  }  // namespace Internal

  template<typename T>
  class Section final : public SectionBase {
  public:
    explicit Section(const SectionInfo& info)
      : SectionBase(info)
      , m_dataMutex()
      , m_value()
      , m_staged()
    {
    }

    /// @brief A copy of the current value.
    [[nodiscard]] T Get() const
    {
      ScopedLock lock__(&m_dataMutex);
      return m_value;
    }

    /// @brief Runs `fn(const T&)` under the read lock and returns what it returns: for reading one field of a large
    ///        section without copying it. Keep `fn` short, and never call into config from it.
    template<typename Fn>
    decltype(auto) Read(Fn&& fn) const
    {
      ScopedLock lock__(&m_dataMutex);
      return std::forward<Fn>(fn)(std::as_const(m_value));
    }

    bool Set(const T& value)
    {
      return Update([&](T& current) {
        current = value;
        return true;
      });
    }

    /// @brief Read-modify-write that no other writer can interleave with. `mutate` edits a copy of the current value
    ///        and returns whether it changed anything; a changed copy is saved (the keys that differ) and only then
    ///        published. It runs under the write lock: keep it to plain field updates.
    /// @return False only when saving failed, in which case the value is unchanged.
    bool Update(Util::FunctionRef<bool(T&)> mutate)
    {
      ScopedLock lock__(&Internal::WriteLock());
      T updated = m_value;  // Writers are serialized, so the value can't change under this read
      if (!mutate(updated)) {
        return true;
      }
      if (!save(updated, &m_value)) {
        return false;
      }
      publish(std::move(updated));
      return true;
    }

    bool Load(const char* partition) override
    {
      T loaded;
      bool ok = loaded.LoadNvs(partition);
      if (!ok) {
        loaded.ToDefault();
      }
      publish(std::move(loaded));
      return ok;
    }

    bool Reset(const char* partition) override
    {
      T defaults;
      defaults.ToDefault();
      bool ok = true;
      if (partition != nullptr) {
        ok = eraseNamespaces(partition) && defaults.SaveNvs(partition, nullptr);
      }
      publish(std::move(defaults));  // What the hub runs with either way; a failed save only means it isn't kept
      return ok;
    }

    bool VerifyStored(const char* partition) const override
    {
      T stored;
      return stored.LoadNvs(partition) && sameValue(stored, Get());
    }

    void ToJSON(json_gen_str_t* gen, bool withSensitiveData) const override
    {
      ScopedLock lock__(&m_dataMutex);
      m_value.ToJSON(gen, Info().name, withSensitiveData);
    }

    bool Stage(const JSON::JsonView& json) override
    {
      T staged = m_value;  // FromJSON resets every field it is given no value for
      if (!staged.FromJSON(json)) {
        return false;
      }
      m_staged.emplace(std::move(staged));
      return true;
    }

    bool CommitStaged(const char* partition, bool full) override
    {
      if (!m_staged) {
        return true;
      }
      bool ok = partition == nullptr || m_staged->SaveNvs(partition, full ? nullptr : &m_value);
      if (ok) {
        publish(std::move(*m_staged));
      }
      m_staged.reset();
      return ok;
    }

    void DiscardStaged() override { m_staged.reset(); }

    uint32_t ToFlatbuffers(flatbuffers::FlatBufferBuilder& builder, bool withSensitiveData) const override
    {
      ScopedLock lock__(&m_dataMutex);
      return m_value.ToFlatbuffers(builder, withSensitiveData).o;
    }

  private:
    bool save(const T& value, const T* previous) const
    {
      const char* partition = Internal::Partition();
      return partition == nullptr || value.SaveNvs(partition, previous);
    }

    void publish(T&& value)
    {
      ScopedLock lock__(&m_dataMutex);
      m_value = std::move(value);
    }

    bool eraseNamespaces(const char* partition) const;
    static bool sameValue(const T& a, const T& b);

    mutable SimpleMutex m_dataMutex;
    T m_value;
    std::optional<T> m_staged;  // Only touched under the write lock
  };

  namespace Internal {
    bool EraseNamespaces(const char* partition, std::span<const char* const> namespaces);
    /// @brief Whether two values render the same JSON, secrets included: the complete, canonical comparison.
    bool SameJson(Util::FunctionRef<void(json_gen_str_t*)> renderA, Util::FunctionRef<void(json_gen_str_t*)> renderB);
  }  // namespace Internal

  template<typename T>
  bool Section<T>::eraseNamespaces(const char* partition) const
  {
    return Internal::EraseNamespaces(partition, Info().namespaces);
  }

  template<typename T>
  bool Section<T>::sameValue(const T& a, const T& b)
  {
    return Internal::SameJson([&](json_gen_str_t* gen) { a.ToJSON(gen, nullptr, true); }, [&](json_gen_str_t* gen) { b.ToJSON(gen, nullptr, true); });
  }
}  // namespace OpenShock::Config
