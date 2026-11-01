#pragma once

// A config section: one module's settings, owned and defined by that module, kept in NVS through this framework.
//
// The module defines a value type T meeting SectionValue (below) and one Section<T> object for it (a namespace-scope
// static in its own .cpp):
//
//   struct RFConfig {
//     RFConfig();                                                       // the defaults, defined nowhere else
//     void Normalize();                                                 // validation, run on every value taken in
//     bool LoadNvs(const char* partition);                              // missing keys are defaults
//     bool SaveNvs(const char* partition, const RFConfig* previous) const;  // writes only what differs
//     bool FromJSON(JSON::JsonView json);                               // invalid view: defaults
//     void ToJSON(json_gen_str_t* gen, const char* name, bool withSensitiveData) const;
//   };
//   static constexpr const char* kNamespaces[] = {"rf"};
//   static Config::Section<RFConfig> s_rf({.name = "rf", .order = 0, .namespaces = kNamespaces});
//
// and wraps it in whatever accessors it wants (Config::GetRFConfigTxPin() and so on). Nothing outside the module
// needs to know T's fields or keys; this framework only sees the section through SectionBase.
//
// The section caches its value in RAM: readers copy it out under a short lock and never wait on flash. Writers are
// serialized across every section (one write lock), save what changed, and only then publish the new value. NVS writes
// land key by key, so a failed save can leave some of them on flash; the section then no longer trusts its cache as a
// picture of flash, and its next save rewrites every key. A section that failed to load is treated the same way.
//
// Every section links itself into one registry when it is constructed (static initialization), which is how the
// framework loads all of them at boot, renders the whole config as JSON (jsonconfig), imports one, and resets them
// (factory reset). Sections never die; the registry is a constant-initialized intrusive list.

#include "config/Store.h"
#include "OpenShock.h"
#include "SimpleMutex.h"
#include "util/FunctionRef.h"

#include <json_generator.h>

#include <concepts>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace OpenShock::JSON {
  class JsonView;
}

namespace OpenShock::Config {
  struct SectionInfo {
    const char* name;                         // member name of the section in the jsonconfig document, e.g. "rf"
    uint8_t order;                            // position in that document; unique across sections
    std::span<const char* const> namespaces;  // the NVS namespaces it owns (erased by a reset)
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
    virtual bool Load()                                                    = 0;  // from NVS into the cache; false on a read error
    virtual bool Reset()                                                   = 0;  // erase its namespaces and publish the defaults
    virtual void ToJSON(json_gen_str_t* gen, bool withSensitiveData) const = 0;  // as the member `name`
    virtual bool Stage(const JSON::JsonView& json)                         = 0;  // parse into a staging copy
    virtual bool CommitStaged()                                            = 0;  // save it and publish
    virtual void DiscardStaged()                                           = 0;

  protected:
    explicit SectionBase(const SectionInfo& info);
    ~SectionBase() = default;  // Sections are statics and are never destroyed

  private:
    const SectionInfo m_info;  // A copy: modules pass a temporary
    SectionBase* m_next;
  };

  /// @brief What a section's value type provides; no base class, checked where its Section is defined. A default
  ///        constructed value is the defaults. Normalize() is the type's validation: the section runs it on every value
  ///        it takes in (load, update, import, reset), so no setter can skip it.
  template<typename T>
  concept SectionValue = std::default_initializable<T> && std::copyable<T> && requires(T& value, const T& constValue, const char* partition, json_gen_str_t* gen, bool withSensitiveData) {
    { value.Normalize() } -> std::same_as<void>;
    { value.LoadNvs(partition) } -> std::same_as<bool>;
    { constValue.SaveNvs(partition, &constValue) } -> std::same_as<bool>;
    { &T::FromJSON } -> std::same_as<bool (T::*)(JSON::JsonView)>;  // JsonView may be incomplete here
    { constValue.ToJSON(gen, "name", withSensitiveData) } -> std::same_as<void>;
  };

  namespace Internal {
    /// @brief Serializes every writer of every section, the store's own operations included.
    SimpleMutex& WriteLock();
    bool EraseNamespaces(const char* partition, std::span<const char* const> namespaces);
  }  // namespace Internal

  template<SectionValue T>
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
    /// @return False only when saving failed, in which case the running value is unchanged.
    bool Update(Util::FunctionRef<bool(T&)> mutate)
    {
      ScopedLock lock__(&Internal::WriteLock());
      T updated = m_value;  // Writers are serialized, so the value can't change under this read
      if (!mutate(updated)) {
        return true;
      }
      updated.Normalize();
      if (!save(updated)) {
        return false;
      }
      publish(std::move(updated));
      return true;
    }

    bool Load() override
    {
      const char* partition = StorageLabel();
      T loaded;
      bool ok = partition == nullptr || loaded.LoadNvs(partition);
      if (!ok) {
        loaded = T();
      }
      loaded.Normalize();
      m_unsynced = !ok;
      publish(std::move(loaded));
      return ok;
    }

    bool Reset() override
    {
      const char* partition = StorageLabel();
      T value;
      value.Normalize();
      bool ok    = partition == nullptr || Internal::EraseNamespaces(partition, Info().namespaces);
      m_unsynced = !ok;
      publish(std::move(value));  // What the hub runs with either way; a failed erase only means flash still differs
      return ok;
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
      staged.Normalize();
      m_staged.emplace(std::move(staged));
      return true;
    }

    bool CommitStaged() override
    {
      if (!m_staged) {
        return true;
      }
      bool ok = save(*m_staged);
      if (ok) {
        publish(std::move(*m_staged));
      }
      m_staged.reset();
      return ok;
    }

    void DiscardStaged() override { m_staged.reset(); }

  private:
    // Only the keys that differ from the cache, or every key when flash may not match it
    bool save(const T& value)
    {
      const char* partition = StorageLabel();
      if (partition == nullptr) {
        return true;
      }
      m_unsynced = !value.SaveNvs(partition, m_unsynced ? nullptr : &m_value);
      return !m_unsynced;
    }

    void publish(T&& value)
    {
      ScopedLock lock__(&m_dataMutex);
      m_value = std::move(value);
    }

    mutable SimpleMutex m_dataMutex;
    T m_value;
    std::optional<T> m_staged;  // Only touched under the write lock
    bool m_unsynced = false;    // Flash may differ from m_value, so the next save writes every key. Write lock only.
  };

}  // namespace OpenShock::Config
