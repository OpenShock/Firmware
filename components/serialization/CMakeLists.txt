# OpenShock serialization layer.
#
# For now this is the generated flatbuffer wire types (serialization/_fbs/*_generated.h,
# produced by scripts/generate_schemas.py from schemas/*.fbs) — a single flatbuffers-only
# layer that config, wifi, the message codecs and the handlers all depend on one-way.
# Extracting these out of `core` is what lets config (and later the codecs themselves:
# JsonAPI/JsonSerial/WSGateway/WSLocal) leave core without a dependency cycle.
idf_component_register(
  INCLUDE_DIRS "include"
  REQUIRES     flatbuffers    # flatbuffers/flatbuffers.h in the generated headers
)
