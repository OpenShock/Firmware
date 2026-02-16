#pragma once

#include <cstdint>
#include <utility>

namespace OpenShock::Util {
  namespace FnProxyImpl {
    template<typename T>
    class MemFunTraits;

    template<typename R, typename C, typename... Ts>
    struct MemFunTraits<R (C::*)(Ts...)> {
      constexpr static const std::size_t arity = sizeof...(Ts);
      using ReturnType                         = R;
      using Class                              = C;
    };
  }  // namespace FnProxyImpl

  // Proxies a member function pointer to a void(void*) function pointer, allowing it to be passed to C-style APIs that expect a callback.
  template<auto MemberFunction, typename... Args>
  inline void FnProxy(void* p, Args... args)
  {
    using T = decltype(MemberFunction);
    using C = typename FnProxyImpl::MemFunTraits<T>::Class;
    (reinterpret_cast<C*>(p)->*MemberFunction)(std::forward<Args>(args)...);
  }
}  // namespace OpenShock::Util
