#pragma once

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace OpenShock::Util {

  template<class Sig>
  class FunctionRef;

  /// Non-owning reference to a callable, modelled on C++26 std::function_ref
  /// (swap it in once libstdc++ ships it; GCC 15 does not have it yet).
  ///
  /// Two pointers wide and trivially copyable: the referenced object (or the
  /// function pointer itself) plus a thunk that knows its real type. Use it for
  /// callback parameters that are only invoked during the call they are passed
  /// to; it never allocates, unlike std::function.
  ///
  /// It does NOT extend the callable's lifetime. Never store a FunctionRef beyond
  /// the call it was passed to (no members, queues or task captures): a lambda
  /// temporary dies at the end of the caller's full-expression.
  ///
  /// Usage:
  ///   void ForEach(FunctionRef<bool(int)> fn);
  ///   ForEach([&](int x) { return x == wanted; });
  template<class R, class... A>
  class FunctionRef<R(A...)> {
    union Storage {
      void* obj;
      void (*fn)();
    };

    template<class F>
    static constexpr bool IsFunction = std::is_function_v<std::remove_pointer_t<std::remove_cvref_t<F>>>;

  public:
    template<class F>
      requires(!std::is_same_v<std::remove_cvref_t<F>, FunctionRef> && std::is_invocable_r_v<R, std::remove_reference_t<F>&, A...>)
    FunctionRef(F&& f) noexcept  // NOLINT(google-explicit-constructor): implicit like std::function_ref
    {
      if constexpr (IsFunction<F>) {
        // Function or function pointer: store the pointer itself, never the address of the caller's temporary.
        using Fn     = std::remove_pointer_t<std::remove_cvref_t<F>>*;
        Fn fn        = f;
        m_storage.fn = reinterpret_cast<void (*)()>(fn);
        m_thunk      = [](Storage s, A... args) -> R { return Invoke(reinterpret_cast<Fn>(s.fn), std::forward<A>(args)...); };
      } else {
        // Callable object: keep its address and cast back with the same constness on invocation.
        using T       = std::remove_reference_t<F>;
        m_storage.obj = const_cast<void*>(static_cast<const volatile void*>(std::addressof(f)));
        m_thunk       = [](Storage s, A... args) -> R { return Invoke(*static_cast<T*>(s.obj), std::forward<A>(args)...); };
      }
    }

    R operator()(A... args) const { return m_thunk(m_storage, std::forward<A>(args)...); }

  private:
    template<class F>
    static R Invoke(F&& f, A&&... args)
    {
      if constexpr (std::is_void_v<R>) {
        std::invoke(std::forward<F>(f), std::forward<A>(args)...);
      } else {
        return std::invoke(std::forward<F>(f), std::forward<A>(args)...);
      }
    }

    Storage m_storage;
    R (*m_thunk)(Storage, A...);
  };
}  // namespace OpenShock::Util
