# Vendored littlefs core (upstream ARM littlefs, BSD-3-Clause). This is the plain
# filesystem library only — no VFS layer. OpenShock mounts partitions against it
# directly via an esp_partition-backed block device (see the captive portal's
# StaticFs helper). Sources copied from github.com/littlefs-project/littlefs.
idf_component_register(
  SRCS "lfs.c" "lfs_util.c"
  INCLUDE_DIRS "."
)

# Quiet the library: it otherwise emits printf-based trace/debug/warn/error output.
target_compile_definitions(${COMPONENT_LIB} PUBLIC
  LFS_NO_DEBUG
  LFS_NO_WARN
  LFS_NO_ERROR
  LFS_NO_ASSERT
)
