# The ESP-IDF partition table as data: parse the binary table, tell which OpenShock layout it is, and check a
# staged table against the invariants the over-the-air table rewrite relies on (docs/STORAGE.md). Pure C++ over
# byte spans with its own MD5, so it has no ESP-IDF dependency and the host tests run it against every table in
# partitions/ plus a corpus of broken ones. The device side (reading the live table, writing the new one) is in
# components/migration.
idf_component_register(
  SRCS          "src/PartitionTable.cpp"
                "src/Md5.cpp"
  INCLUDE_DIRS  "include"
  REQUIRES      common
)
