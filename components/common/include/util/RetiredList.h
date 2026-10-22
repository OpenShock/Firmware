#pragma once

#include "OpenShock.h"
#include "SimpleMutex.h"

#include <memory>
#include <vector>

namespace OpenShock {
  /// @brief Objects taken out of service whose destruction must run on one owning task.
  ///
  /// Other tasks may still hold a std::shared_ptr copy for a moment (e.g. while sending through it). Dropping that copy
  /// would otherwise run the destructor on whichever task happened to hold it last, and destructors that stop servers
  /// or tasks must not run on an event-loop, httpd or websocket task. Retired objects stay referenced here until the
  /// owner calls reap(), which destroys, on the calling task, every one that nobody else holds any more.
  template<typename T>
  class RetiredList {
    DISABLE_COPY(RetiredList);
    DISABLE_MOVE(RetiredList);

  public:
    RetiredList() = default;

    void retire(std::shared_ptr<T> object)
    {
      if (object == nullptr) {
        return;
      }

      ScopedLock lock__(&m_mutex);
      m_objects.push_back(std::move(object));
    }

    /// @brief Destroys the retired objects no other task still references. Returns true once none are left.
    bool reap()
    {
      std::vector<std::shared_ptr<T>> reapable;
      bool empty;
      {
        ScopedLock lock__(&m_mutex);
        for (auto it = m_objects.begin(); it != m_objects.end();) {
          // Nothing hands out new references to a retired object, so a use_count of 1 means this list is the sole owner.
          if (it->use_count() == 1) {
            reapable.push_back(std::move(*it));
            it = m_objects.erase(it);
          } else {
            ++it;
          }
        }
        empty = m_objects.empty();
      }
      // `reapable` goes out of scope here, outside the lock, running the destructors on this task.
      return empty;
    }

  private:
    SimpleMutex m_mutex;
    std::vector<std::shared_ptr<T>> m_objects;
  };
}  // namespace OpenShock
