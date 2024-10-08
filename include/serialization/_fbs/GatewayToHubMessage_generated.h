// automatically generated by the FlatBuffers compiler, do not modify


#ifndef FLATBUFFERS_GENERATED_GATEWAYTOHUBMESSAGE_OPENSHOCK_SERIALIZATION_GATEWAY_H_
#define FLATBUFFERS_GENERATED_GATEWAYTOHUBMESSAGE_OPENSHOCK_SERIALIZATION_GATEWAY_H_

#include "flatbuffers/flatbuffers.h"

// Ensure the included flatbuffers.h is the same version as when this file was
// generated, otherwise it may not be compatible.
static_assert(FLATBUFFERS_VERSION_MAJOR == 24 &&
              FLATBUFFERS_VERSION_MINOR == 3 &&
              FLATBUFFERS_VERSION_REVISION == 25,
             "Non-compatible flatbuffers version included");

#include "SemVer_generated.h"
#include "ShockerCommandType_generated.h"
#include "ShockerModelType_generated.h"

namespace OpenShock {
namespace Serialization {
namespace Gateway {

struct ShockerCommand;

struct ShockerCommandList;
struct ShockerCommandListBuilder;

struct CaptivePortalConfig;

struct OtaInstall;
struct OtaInstallBuilder;

struct GatewayToHubMessage;
struct GatewayToHubMessageBuilder;

enum class GatewayToHubMessagePayload : uint8_t {
  NONE = 0,
  ShockerCommandList = 1,
  CaptivePortalConfig = 2,
  OtaInstall = 3,
  MIN = NONE,
  MAX = OtaInstall
};

inline const GatewayToHubMessagePayload (&EnumValuesGatewayToHubMessagePayload())[4] {
  static const GatewayToHubMessagePayload values[] = {
    GatewayToHubMessagePayload::NONE,
    GatewayToHubMessagePayload::ShockerCommandList,
    GatewayToHubMessagePayload::CaptivePortalConfig,
    GatewayToHubMessagePayload::OtaInstall
  };
  return values;
}

inline const char * const *EnumNamesGatewayToHubMessagePayload() {
  static const char * const names[5] = {
    "NONE",
    "ShockerCommandList",
    "CaptivePortalConfig",
    "OtaInstall",
    nullptr
  };
  return names;
}

inline const char *EnumNameGatewayToHubMessagePayload(GatewayToHubMessagePayload e) {
  if (::flatbuffers::IsOutRange(e, GatewayToHubMessagePayload::NONE, GatewayToHubMessagePayload::OtaInstall)) return "";
  const size_t index = static_cast<size_t>(e);
  return EnumNamesGatewayToHubMessagePayload()[index];
}

template<typename T> struct GatewayToHubMessagePayloadTraits {
  static const GatewayToHubMessagePayload enum_value = GatewayToHubMessagePayload::NONE;
};

template<> struct GatewayToHubMessagePayloadTraits<OpenShock::Serialization::Gateway::ShockerCommandList> {
  static const GatewayToHubMessagePayload enum_value = GatewayToHubMessagePayload::ShockerCommandList;
};

template<> struct GatewayToHubMessagePayloadTraits<OpenShock::Serialization::Gateway::CaptivePortalConfig> {
  static const GatewayToHubMessagePayload enum_value = GatewayToHubMessagePayload::CaptivePortalConfig;
};

template<> struct GatewayToHubMessagePayloadTraits<OpenShock::Serialization::Gateway::OtaInstall> {
  static const GatewayToHubMessagePayload enum_value = GatewayToHubMessagePayload::OtaInstall;
};

bool VerifyGatewayToHubMessagePayload(::flatbuffers::Verifier &verifier, const void *obj, GatewayToHubMessagePayload type);
bool VerifyGatewayToHubMessagePayloadVector(::flatbuffers::Verifier &verifier, const ::flatbuffers::Vector<::flatbuffers::Offset<void>> *values, const ::flatbuffers::Vector<GatewayToHubMessagePayload> *types);

FLATBUFFERS_MANUALLY_ALIGNED_STRUCT(2) ShockerCommand FLATBUFFERS_FINAL_CLASS {
 private:
  uint8_t model_;
  int8_t padding0__;
  uint16_t id_;
  uint8_t type_;
  uint8_t intensity_;
  uint16_t duration_;

 public:
  struct Traits;
  static FLATBUFFERS_CONSTEXPR_CPP11 const char *GetFullyQualifiedName() {
    return "OpenShock.Serialization.Gateway.ShockerCommand";
  }
  ShockerCommand()
      : model_(0),
        padding0__(0),
        id_(0),
        type_(0),
        intensity_(0),
        duration_(0) {
    (void)padding0__;
  }
  ShockerCommand(OpenShock::Serialization::Types::ShockerModelType _model, uint16_t _id, OpenShock::Serialization::Types::ShockerCommandType _type, uint8_t _intensity, uint16_t _duration)
      : model_(::flatbuffers::EndianScalar(static_cast<uint8_t>(_model))),
        padding0__(0),
        id_(::flatbuffers::EndianScalar(_id)),
        type_(::flatbuffers::EndianScalar(static_cast<uint8_t>(_type))),
        intensity_(::flatbuffers::EndianScalar(_intensity)),
        duration_(::flatbuffers::EndianScalar(_duration)) {
    (void)padding0__;
  }
  OpenShock::Serialization::Types::ShockerModelType model() const {
    return static_cast<OpenShock::Serialization::Types::ShockerModelType>(::flatbuffers::EndianScalar(model_));
  }
  uint16_t id() const {
    return ::flatbuffers::EndianScalar(id_);
  }
  OpenShock::Serialization::Types::ShockerCommandType type() const {
    return static_cast<OpenShock::Serialization::Types::ShockerCommandType>(::flatbuffers::EndianScalar(type_));
  }
  uint8_t intensity() const {
    return ::flatbuffers::EndianScalar(intensity_);
  }
  uint16_t duration() const {
    return ::flatbuffers::EndianScalar(duration_);
  }
};
FLATBUFFERS_STRUCT_END(ShockerCommand, 8);

struct ShockerCommand::Traits {
  using type = ShockerCommand;
};

FLATBUFFERS_MANUALLY_ALIGNED_STRUCT(1) CaptivePortalConfig FLATBUFFERS_FINAL_CLASS {
 private:
  uint8_t enabled_;

 public:
  struct Traits;
  static FLATBUFFERS_CONSTEXPR_CPP11 const char *GetFullyQualifiedName() {
    return "OpenShock.Serialization.Gateway.CaptivePortalConfig";
  }
  CaptivePortalConfig()
      : enabled_(0) {
  }
  CaptivePortalConfig(bool _enabled)
      : enabled_(::flatbuffers::EndianScalar(static_cast<uint8_t>(_enabled))) {
  }
  bool enabled() const {
    return ::flatbuffers::EndianScalar(enabled_) != 0;
  }
};
FLATBUFFERS_STRUCT_END(CaptivePortalConfig, 1);

struct CaptivePortalConfig::Traits {
  using type = CaptivePortalConfig;
};

struct ShockerCommandList FLATBUFFERS_FINAL_CLASS : private ::flatbuffers::Table {
  typedef ShockerCommandListBuilder Builder;
  struct Traits;
  static FLATBUFFERS_CONSTEXPR_CPP11 const char *GetFullyQualifiedName() {
    return "OpenShock.Serialization.Gateway.ShockerCommandList";
  }
  enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
    VT_COMMANDS = 4
  };
  const ::flatbuffers::Vector<const OpenShock::Serialization::Gateway::ShockerCommand *> *commands() const {
    return GetPointer<const ::flatbuffers::Vector<const OpenShock::Serialization::Gateway::ShockerCommand *> *>(VT_COMMANDS);
  }
  bool Verify(::flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyOffsetRequired(verifier, VT_COMMANDS) &&
           verifier.VerifyVector(commands()) &&
           verifier.EndTable();
  }
};

struct ShockerCommandListBuilder {
  typedef ShockerCommandList Table;
  ::flatbuffers::FlatBufferBuilder &fbb_;
  ::flatbuffers::uoffset_t start_;
  void add_commands(::flatbuffers::Offset<::flatbuffers::Vector<const OpenShock::Serialization::Gateway::ShockerCommand *>> commands) {
    fbb_.AddOffset(ShockerCommandList::VT_COMMANDS, commands);
  }
  explicit ShockerCommandListBuilder(::flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  ::flatbuffers::Offset<ShockerCommandList> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = ::flatbuffers::Offset<ShockerCommandList>(end);
    fbb_.Required(o, ShockerCommandList::VT_COMMANDS);
    return o;
  }
};

inline ::flatbuffers::Offset<ShockerCommandList> CreateShockerCommandList(
    ::flatbuffers::FlatBufferBuilder &_fbb,
    ::flatbuffers::Offset<::flatbuffers::Vector<const OpenShock::Serialization::Gateway::ShockerCommand *>> commands = 0) {
  ShockerCommandListBuilder builder_(_fbb);
  builder_.add_commands(commands);
  return builder_.Finish();
}

struct ShockerCommandList::Traits {
  using type = ShockerCommandList;
  static auto constexpr Create = CreateShockerCommandList;
};

inline ::flatbuffers::Offset<ShockerCommandList> CreateShockerCommandListDirect(
    ::flatbuffers::FlatBufferBuilder &_fbb,
    const std::vector<OpenShock::Serialization::Gateway::ShockerCommand> *commands = nullptr) {
  auto commands__ = commands ? _fbb.CreateVectorOfStructs<OpenShock::Serialization::Gateway::ShockerCommand>(*commands) : 0;
  return OpenShock::Serialization::Gateway::CreateShockerCommandList(
      _fbb,
      commands__);
}

struct OtaInstall FLATBUFFERS_FINAL_CLASS : private ::flatbuffers::Table {
  typedef OtaInstallBuilder Builder;
  struct Traits;
  static FLATBUFFERS_CONSTEXPR_CPP11 const char *GetFullyQualifiedName() {
    return "OpenShock.Serialization.Gateway.OtaInstall";
  }
  enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
    VT_VERSION = 4
  };
  const OpenShock::Serialization::Types::SemVer *version() const {
    return GetPointer<const OpenShock::Serialization::Types::SemVer *>(VT_VERSION);
  }
  bool Verify(::flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyOffset(verifier, VT_VERSION) &&
           verifier.VerifyTable(version()) &&
           verifier.EndTable();
  }
};

struct OtaInstallBuilder {
  typedef OtaInstall Table;
  ::flatbuffers::FlatBufferBuilder &fbb_;
  ::flatbuffers::uoffset_t start_;
  void add_version(::flatbuffers::Offset<OpenShock::Serialization::Types::SemVer> version) {
    fbb_.AddOffset(OtaInstall::VT_VERSION, version);
  }
  explicit OtaInstallBuilder(::flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  ::flatbuffers::Offset<OtaInstall> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = ::flatbuffers::Offset<OtaInstall>(end);
    return o;
  }
};

inline ::flatbuffers::Offset<OtaInstall> CreateOtaInstall(
    ::flatbuffers::FlatBufferBuilder &_fbb,
    ::flatbuffers::Offset<OpenShock::Serialization::Types::SemVer> version = 0) {
  OtaInstallBuilder builder_(_fbb);
  builder_.add_version(version);
  return builder_.Finish();
}

struct OtaInstall::Traits {
  using type = OtaInstall;
  static auto constexpr Create = CreateOtaInstall;
};

struct GatewayToHubMessage FLATBUFFERS_FINAL_CLASS : private ::flatbuffers::Table {
  typedef GatewayToHubMessageBuilder Builder;
  struct Traits;
  static FLATBUFFERS_CONSTEXPR_CPP11 const char *GetFullyQualifiedName() {
    return "OpenShock.Serialization.Gateway.GatewayToHubMessage";
  }
  enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {
    VT_PAYLOAD_TYPE = 4,
    VT_PAYLOAD = 6
  };
  OpenShock::Serialization::Gateway::GatewayToHubMessagePayload payload_type() const {
    return static_cast<OpenShock::Serialization::Gateway::GatewayToHubMessagePayload>(GetField<uint8_t>(VT_PAYLOAD_TYPE, 0));
  }
  const void *payload() const {
    return GetPointer<const void *>(VT_PAYLOAD);
  }
  template<typename T> const T *payload_as() const;
  const OpenShock::Serialization::Gateway::ShockerCommandList *payload_as_ShockerCommandList() const {
    return payload_type() == OpenShock::Serialization::Gateway::GatewayToHubMessagePayload::ShockerCommandList ? static_cast<const OpenShock::Serialization::Gateway::ShockerCommandList *>(payload()) : nullptr;
  }
  const OpenShock::Serialization::Gateway::CaptivePortalConfig *payload_as_CaptivePortalConfig() const {
    return payload_type() == OpenShock::Serialization::Gateway::GatewayToHubMessagePayload::CaptivePortalConfig ? static_cast<const OpenShock::Serialization::Gateway::CaptivePortalConfig *>(payload()) : nullptr;
  }
  const OpenShock::Serialization::Gateway::OtaInstall *payload_as_OtaInstall() const {
    return payload_type() == OpenShock::Serialization::Gateway::GatewayToHubMessagePayload::OtaInstall ? static_cast<const OpenShock::Serialization::Gateway::OtaInstall *>(payload()) : nullptr;
  }
  bool Verify(::flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyField<uint8_t>(verifier, VT_PAYLOAD_TYPE, 1) &&
           VerifyOffset(verifier, VT_PAYLOAD) &&
           VerifyGatewayToHubMessagePayload(verifier, payload(), payload_type()) &&
           verifier.EndTable();
  }
};

template<> inline const OpenShock::Serialization::Gateway::ShockerCommandList *GatewayToHubMessage::payload_as<OpenShock::Serialization::Gateway::ShockerCommandList>() const {
  return payload_as_ShockerCommandList();
}

template<> inline const OpenShock::Serialization::Gateway::CaptivePortalConfig *GatewayToHubMessage::payload_as<OpenShock::Serialization::Gateway::CaptivePortalConfig>() const {
  return payload_as_CaptivePortalConfig();
}

template<> inline const OpenShock::Serialization::Gateway::OtaInstall *GatewayToHubMessage::payload_as<OpenShock::Serialization::Gateway::OtaInstall>() const {
  return payload_as_OtaInstall();
}

struct GatewayToHubMessageBuilder {
  typedef GatewayToHubMessage Table;
  ::flatbuffers::FlatBufferBuilder &fbb_;
  ::flatbuffers::uoffset_t start_;
  void add_payload_type(OpenShock::Serialization::Gateway::GatewayToHubMessagePayload payload_type) {
    fbb_.AddElement<uint8_t>(GatewayToHubMessage::VT_PAYLOAD_TYPE, static_cast<uint8_t>(payload_type), 0);
  }
  void add_payload(::flatbuffers::Offset<void> payload) {
    fbb_.AddOffset(GatewayToHubMessage::VT_PAYLOAD, payload);
  }
  explicit GatewayToHubMessageBuilder(::flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  ::flatbuffers::Offset<GatewayToHubMessage> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = ::flatbuffers::Offset<GatewayToHubMessage>(end);
    return o;
  }
};

inline ::flatbuffers::Offset<GatewayToHubMessage> CreateGatewayToHubMessage(
    ::flatbuffers::FlatBufferBuilder &_fbb,
    OpenShock::Serialization::Gateway::GatewayToHubMessagePayload payload_type = OpenShock::Serialization::Gateway::GatewayToHubMessagePayload::NONE,
    ::flatbuffers::Offset<void> payload = 0) {
  GatewayToHubMessageBuilder builder_(_fbb);
  builder_.add_payload(payload);
  builder_.add_payload_type(payload_type);
  return builder_.Finish();
}

struct GatewayToHubMessage::Traits {
  using type = GatewayToHubMessage;
  static auto constexpr Create = CreateGatewayToHubMessage;
};

inline bool VerifyGatewayToHubMessagePayload(::flatbuffers::Verifier &verifier, const void *obj, GatewayToHubMessagePayload type) {
  switch (type) {
    case GatewayToHubMessagePayload::NONE: {
      return true;
    }
    case GatewayToHubMessagePayload::ShockerCommandList: {
      auto ptr = reinterpret_cast<const OpenShock::Serialization::Gateway::ShockerCommandList *>(obj);
      return verifier.VerifyTable(ptr);
    }
    case GatewayToHubMessagePayload::CaptivePortalConfig: {
      return verifier.VerifyField<OpenShock::Serialization::Gateway::CaptivePortalConfig>(static_cast<const uint8_t *>(obj), 0, 1);
    }
    case GatewayToHubMessagePayload::OtaInstall: {
      auto ptr = reinterpret_cast<const OpenShock::Serialization::Gateway::OtaInstall *>(obj);
      return verifier.VerifyTable(ptr);
    }
    default: return true;
  }
}

inline bool VerifyGatewayToHubMessagePayloadVector(::flatbuffers::Verifier &verifier, const ::flatbuffers::Vector<::flatbuffers::Offset<void>> *values, const ::flatbuffers::Vector<GatewayToHubMessagePayload> *types) {
  if (!values || !types) return !values && !types;
  if (values->size() != types->size()) return false;
  for (::flatbuffers::uoffset_t i = 0; i < values->size(); ++i) {
    if (!VerifyGatewayToHubMessagePayload(
        verifier,  values->Get(i), types->GetEnum<GatewayToHubMessagePayload>(i))) {
      return false;
    }
  }
  return true;
}

inline const OpenShock::Serialization::Gateway::GatewayToHubMessage *GetGatewayToHubMessage(const void *buf) {
  return ::flatbuffers::GetRoot<OpenShock::Serialization::Gateway::GatewayToHubMessage>(buf);
}

inline const OpenShock::Serialization::Gateway::GatewayToHubMessage *GetSizePrefixedGatewayToHubMessage(const void *buf) {
  return ::flatbuffers::GetSizePrefixedRoot<OpenShock::Serialization::Gateway::GatewayToHubMessage>(buf);
}

inline bool VerifyGatewayToHubMessageBuffer(
    ::flatbuffers::Verifier &verifier) {
  return verifier.VerifyBuffer<OpenShock::Serialization::Gateway::GatewayToHubMessage>(nullptr);
}

inline bool VerifySizePrefixedGatewayToHubMessageBuffer(
    ::flatbuffers::Verifier &verifier) {
  return verifier.VerifySizePrefixedBuffer<OpenShock::Serialization::Gateway::GatewayToHubMessage>(nullptr);
}

inline void FinishGatewayToHubMessageBuffer(
    ::flatbuffers::FlatBufferBuilder &fbb,
    ::flatbuffers::Offset<OpenShock::Serialization::Gateway::GatewayToHubMessage> root) {
  fbb.Finish(root);
}

inline void FinishSizePrefixedGatewayToHubMessageBuffer(
    ::flatbuffers::FlatBufferBuilder &fbb,
    ::flatbuffers::Offset<OpenShock::Serialization::Gateway::GatewayToHubMessage> root) {
  fbb.FinishSizePrefixed(root);
}

}  // namespace Gateway
}  // namespace Serialization
}  // namespace OpenShock

#endif  // FLATBUFFERS_GENERATED_GATEWAYTOHUBMESSAGE_OPENSHOCK_SERIALIZATION_GATEWAY_H_
