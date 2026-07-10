# OpenShock concurrency primitives on top of FreeRTOS semaphores.
#
# Public surface:
#   SimpleMutex.h    — scoped binary mutex (ScopedLock)
#   ReadWriteMutex.h — single-writer/multi-reader counting mutex

idf_component_register(
  SRCS         "src/SimpleMutex.cpp"
               "src/ReadWriteMutex.cpp"
  INCLUDE_DIRS "include"
  REQUIRES      freertos                        # semphr.h in public headers
                common                          # OpenShock.h in public headers
  PRIV_REQUIRES logging                         # Logging.h (ReadWriteMutex.cpp only)
)
