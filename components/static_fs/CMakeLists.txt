# Read-only littlefs partition reader over a raw esp_partition (no VFS registration),
# built on the vendored littlefs core. Streams files out to a caller-provided sink.
idf_component_register(
  SRCS "src/StaticFs.cpp"
  INCLUDE_DIRS "include"
  REQUIRES common littlefs esp_partition logging
)
