#pragma once

#include <rybench/serializer/serializer.hpp>
#include <rybench/common/flatbuffers_schema.hpp>
#include <google/protobuf/message.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/reflection.h>
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/reflection.h>
#include <memory>
#include <string>

namespace rybench::serializer {

class ProtobufSerializer : public Serializer {
public:
    explicit ProtobufSerializer(const std::string& bfbsPath);

    std::span<const uint8_t> serialize(const common::Message& msg) override;
    std::string name() const override;

private:
    common::FlatBuffersSchema fbsSchema_;
    google::protobuf::DescriptorPool pool_;
    google::protobuf::DynamicMessageFactory factory_;
    const google::protobuf::Descriptor* rootDescriptor_ = nullptr;
    std::vector<uint8_t> buffer_;

    // Build Protobuf descriptor from FlatBuffers schema
    void buildDescriptorFromBfbs(const reflection::Schema* fbSchema);

    // Map FlatBuffers type to Protobuf type
    google::protobuf::FieldDescriptorProto::Type mapFbTypeToProtoType(
        reflection::BaseType fbType) const;

    // Map FlatBuffers type to Protobuf label
    google::protobuf::FieldDescriptorProto::Label getProtoLabel(
        const reflection::Field* field) const;

    // FlatBuffer to Protobuf conversion helpers
    void populateFromFlatBuffer(google::protobuf::Message* protoMsg,
                                const flatbuffers::Table* fbTable,
                                const reflection::Object* fbObj) const;
    void setProtoFieldFromFlatBuffer(google::protobuf::Message* protoMsg,
                                     const google::protobuf::FieldDescriptor* protoField,
                                     const flatbuffers::Table* fbTable,
                                     const reflection::Field* fbField) const;
};

} // namespace rybench::serializer
