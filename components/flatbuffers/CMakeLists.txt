# Header-only FlatBuffers runtime consumed by the generated _fbs headers
# (#include "flatbuffers/flatbuffers.h"). Wraps the google/flatbuffers submodule,
# pinned to the same commit the PlatformIO lib_deps entry used.
idf_component_register(INCLUDE_DIRS flatbuffers/include)
