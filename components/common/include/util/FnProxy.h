#pragma once

#include <utility>

namespace OpenShock::Util {

  namespace detail {
    // NX carries the source method's noexcept-ness onto the trampoline (and thus
    // onto the resulting function-pointer type); the const specializations bind C
    // as `const C` so the cast matches the method's constness.
    template<auto MF, bool NX, class R, class C, class... A>
    struct Body {
      static R call(void* self, A... args) noexcept(NX)
      { return (static_cast<C*>(self)->*MF)(std::forward<A>(args)...); }
    };
    template<auto MF> struct Deduce;
    template<class R, class C, class... A, R (C::*MF)(A...)>
    struct Deduce<MF> : Body<MF, false, R, C, A...> {};
    template<class R, class C, class... A, R (C::*MF)(A...) const>
    struct Deduce<MF> : Body<MF, false, R, const C, A...> {};
    template<class R, class C, class... A, R (C::*MF)(A...) noexcept>
    struct Deduce<MF> : Body<MF, true, R, C, A...> {};
    template<class R, class C, class... A, R (C::*MF)(A...) const noexcept>
    struct Deduce<MF> : Body<MF, true, R, const C, A...> {};
    // (ref-qualified &/&& members omitted, meaningless for a void* callback.)
  }  // namespace detail

  /// Variable template: FnProxy<&Class::method> is a plain function pointer,
  /// R(*)(void*, Args...), that casts the void* back to Class* and calls the
  /// method. It exists to hand C++ member functions to C callback APIs that pass
  /// context as a void* (FreeRTOS tasks, esp_event handlers).
  ///
  /// Usage:
  ///   auto cb = FnProxy<&Sensor::read>;  // R(*)(void*, float)
  ///   Sensor s;
  ///   cb(&s, 1.5f);
  ///
  /// Works for any non-ref-qualified member function. The four Deduce
  /// specializations cover the const/noexcept combinations, each forwarding the
  /// deduced signature to the single Body::call trampoline whose address becomes
  /// the resulting function pointer. A noexcept method yields a noexcept function
  /// pointer, which still converts to a plain one for C APIs.
  template<auto MF>
  constexpr auto FnProxy = &detail::Deduce<MF>::call;
}  // namespace OpenShock::Util
