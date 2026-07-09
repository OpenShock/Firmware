#pragma once

#include <esp_partition.h>

namespace OpenShock {
  // Verify that `partition` holds a valid, mountable littlefs image by mounting it and
  // immediately unmounting. Returns false for a null partition or any mount failure.
  //
  // Non-destructive: `writable` is passed through to the mount (a read-only mount can
  // never write), and a corrupt image is never reformatted (formatOnFail=false), so
  // this stays a pure check regardless of `writable`. Use it to validate a freshly
  // flashed filesystem image before committing to it.
  bool TestFilesystem(const esp_partition_t* partition, bool writable = false);

  // Convenience overload: find a data partition by label (any subtype) and test it.
  // Returns false if no such partition exists or it fails to mount.
  bool TestFilesystem(const char* partitionLabel, bool writable = false);
}  // namespace OpenShock
