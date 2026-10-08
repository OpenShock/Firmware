# Runs the vendored littlefs core over a RAM block device with the exact geometry and
# mount parameters LfsPartition uses, to check how the small config partition behaves.
idf_component_register(
  SRCS         "test_main.cpp"
               "test_config_partition.cpp"
               "../../../littlefs/src/lfs.c"
               "../../../littlefs/src/lfs_util.c"
  INCLUDE_DIRS "."
               "../../../littlefs/include"
  REQUIRES     unity
  WHOLE_ARCHIVE  # keep the auto-registered TEST_CASEs from being stripped
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LFS_NO_DEBUG LFS_NO_WARN LFS_NO_ERROR LFS_NO_ASSERT)
