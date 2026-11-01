# OpenShock serialization layer.
#
# For now this is the generated flatbuffer wire types (serialization/_fbs/*_generated.h,
# produced by scripts/generate_schemas.py from schemas/*.fbs), a single flatbuffers-only
# layer that the message codecs and the handlers depend on one-way. Extracting these out
# of `core` is what lets the codecs (JsonAPI/JsonSerial/WSGateway/WSLocal) leave core
# later without a dependency cycle.
idf_component_register(
  INCLUDE_DIRS "include"
  REQUIRES     flatbuffers    # flatbuffers/flatbuffers.h in the generated headers
)
