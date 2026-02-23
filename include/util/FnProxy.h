#pragma once

#include <utility>

namespace OpenShock::Util {

  namespace detail {

    template<auto MemberFunction>
    struct FnProxyImpl;

    // Non-const, non-noexcept
    template<typename R, typename C, typename... Ts, R (C::*MF)(Ts...)>
    struct FnProxyImpl<MF> {
      static R Call(void* p, Ts... args) noexcept(noexcept((static_cast<C*>(p)->*MF)(std::move(args)...))) { return (static_cast<C*>(p)->*MF)(std::move(args)...); }
    };

    // Const, non-noexcept
    template<typename R, typename C, typename... Ts, R (C::*MF)(Ts...) const>
    struct FnProxyImpl<MF> {
      static R Call(void* p, Ts... args) noexcept(noexcept((static_cast<const C*>(p)->*MF)(std::move(args)...))) { return (static_cast<const C*>(p)->*MF)(std::move(args)...); }
    };

    // Non-const, noexcept
    template<typename R, typename C, typename... Ts, R (C::*MF)(Ts...) noexcept>
    struct FnProxyImpl<MF> {
      static R Call(void* p, Ts... args) noexcept { return (static_cast<C*>(p)->*MF)(std::move(args)...); }
    };

    // Const, noexcept
    template<typename R, typename C, typename... Ts, R (C::*MF)(Ts...) const noexcept>
    struct FnProxyImpl<MF> {
      static R Call(void* p, Ts... args) noexcept { return (static_cast<const C*>(p)->*MF)(std::move(args)...); }
    };

  }  // namespace detail

  /// Variable template: FnProxy<&Class::method> is directly a function pointer.
  ///
  /// Usage:
  ///   auto cb = FnProxy<&Sensor::read>;  // R(*)(void*, Ts...)
  ///   Sensor s;
  ///   cb(&s, 1.5f);
  template<auto MemberFunction>
  inline constexpr auto FnProxy = &detail::FnProxyImpl<MemberFunction>::Call;

}  // namespace OpenShock::Util
