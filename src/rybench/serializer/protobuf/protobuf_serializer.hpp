#pragma once

#include <rybench/serializer/serializer.hpp>
#include <google/protobuf/message.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/reflection.h>
#include <memory>
#include <string>

#ifdef RYBENCH_ENABLE_FLATBUFFERS
#include "../flatbuffers/flatbuffers_schema.hpp"
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/reflection.h>
#endif

namespace rybench::serializer {

class ProtobufSerializer : public Serializer {
public:
    explicit ProtobufSerializer(const std::string& bfbsPath);

    std::string name() const override;
    core::MessageFormat format() const override;
    std::span<const uint8_t> serialize(const core::Message& msg) override;

    void loadSchema(const core::SchemaDefinition& schema) override;
    const core::SchemaDefinition* loadedSchema() const override {
        return fbsSchema_.isLoaded() ? &fbsSchema_ : nullptr;
    }
    void initMessage() override;
    void addBool  (const core::SchemaField& f, bool val) override;
    void addInt   (const core::SchemaField& f, int64_t val) override;
    void addUInt  (const core::SchemaField& f, uint64_t val) override;
    void addFloat (const core::SchemaField& f, float val) override;
    void addDouble(const core::SchemaField& f, double val) override;
    void addString(const core::SchemaField& f, std::string_view val) override;
    void beginTable (const core::SchemaField& f) override;
    void endTable   () override;
    void beginVector(const core::SchemaField& f, size_t count) override;
    void endVector  () override;
    core::Message finalizeMessage() override;

    // Test hook: a fresh root DynamicMessage the caller can ParseFromArray into,
    // used to decode direct-builder output without touching the private members.
    google::protobuf::Message* newRootInstanceForTest() {
        return rootDescriptor_ ? factory_.GetPrototype(rootDescriptor_)->New() : nullptr;
    }

private:
    google::protobuf::DescriptorPool pool_;
    google::protobuf::DynamicMessageFactory factory_;
    const google::protobuf::Descriptor* rootDescriptor_ = nullptr;
    std::vector<uint8_t> buffer_;

    // ── direct protobuf wire-format builder ─────────────────────────────────────
    // The generator drives initMessage → addX / beginTable / beginVector →
    // finalizeMessage. Instead of populating a DynamicMessage and re-serializing
    // (reflection setters + a second encode pass), we write protobuf wire bytes
    // directly during the walk. Each message level buffers its field instances in
    // walk order, then emits them in field-number order (canonical) on close, so
    // the output is byte-identical to the DynamicMessage path.
    struct BuildFrame {
        const google::protobuf::Descriptor* desc = nullptr;   // message frame: field lookup
        bool     isVector = false;
        bool     packed   = false;                            // vector: proto3 packed scalar
        uint32_t number   = 0;                                // msg frame: number in parent; vec: field number
        const google::protobuf::Descriptor* elemDesc = nullptr;        // vector-of-message element type
        google::protobuf::FieldDescriptor::Type elemType{};            // vector element proto type
        std::vector<uint8_t> body;                            // field instances (walk order) / vector payload
        struct Rec { uint32_t number; uint32_t off; uint32_t len; };
        std::vector<Rec> recs;                                // message frame: one record per emitted field
        void clear() {
            desc = nullptr; isVector = false; packed = false; number = 0;
            elemDesc = nullptr; body.clear(); recs.clear();
        }
    };
    std::vector<BuildFrame> frames_;   // pooled by depth so buffers are reused across messages
    size_t depth_ = 0;                 // live frames = frames_[0 .. depth_-1]

    BuildFrame& pushFrame() {
        if (depth_ == frames_.size()) frames_.emplace_back();
        BuildFrame& f = frames_[depth_++];
        f.clear();
        return f;
    }
    BuildFrame& top() { return frames_[depth_ - 1]; }

    void emitInt   (BuildFrame& fr, const google::protobuf::FieldDescriptor* fd, int64_t val);
    void emitReal  (BuildFrame& fr, const google::protobuf::FieldDescriptor* fd, double  val);
    void emitString(BuildFrame& fr, const google::protobuf::FieldDescriptor* fd, std::string_view val);

#ifdef RYBENCH_ENABLE_FLATBUFFERS
    core::FlatBuffersSchema fbsSchema_;

    void buildDescriptorFromBfbs(const reflection::Schema* fbSchema);

    google::protobuf::FieldDescriptorProto::Type mapFbTypeToProtoType(
        reflection::BaseType fbType) const;

    google::protobuf::FieldDescriptorProto::Label getProtoLabel(
        const reflection::Field* field) const;

    void populateFromFlatBuffer(google::protobuf::Message* protoMsg,
                                const flatbuffers::Table* fbTable,
                                const reflection::Object* fbObj) const;
    void setProtoFieldFromFlatBuffer(google::protobuf::Message* protoMsg,
                                     const google::protobuf::FieldDescriptor* protoField,
                                     const flatbuffers::Table* fbTable,
                                     const reflection::Field* fbField) const;
#endif
};

} // namespace rybench::serializer
