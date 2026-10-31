# littlefs-over-esp_partition filesystem layer (no VFS), built on the vendored littlefs
# core. Provides the shared LfsPartition mount kernel (the captive portal streams its
# pre-built image assets straight from it), the ConfigFs facade (read/write blob store
# for configuration: atomic writes, wear leveling) and FsCheck: non-destructive helpers
# to validate a partition holds a mountable image.
#
# littlefs is a PRIVATE dependency: lfs.h is confined to LfsPartition.cpp so it never
# leaks into consumers of this component's headers.
idf_component_register(
  SRCS "src/LfsPartition.cpp"
       "src/FsCheck.cpp"
  INCLUDE_DIRS "include"
  REQUIRES common esp_partition
  PRIV_REQUIRES littlefs logging
)
