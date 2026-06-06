#include "protobuf_serializer.hpp"
#include <stdexcept>
#include <cctype>
#include <iostream>

namespace rybench::serializer {

ProtobufSerializer::ProtobufSerializer(const std::string& bfbsPath)
    : factory_(&pool_) {
#ifdef RYBENCH_ENABLE_FLATBUFFERS
    if (bfbsPath.empty()) {
        throw std::runtime_error("ProtobufSerializer requires a bfbs schema path");
    }

    fbsSchema_.loadFromBfbs(bfbsPath);

    const auto* fbSchema = fbsSchema_.reflectionSchema();
    if (!fbSchema) {
        throw std::runtime_error("Failed to load FlatBuffers schema from: " + bfbsPath);
    }

    buildDescriptorFromBfbs(fbSchema);
#endif
}

std::string ProtobufSerializer::name() const {
    return "protobuf";
}

core::MessageFormat ProtobufSerializer::format() const {
    return core::MessageFormat::Protobuf;
}

std::span<const uint8_t> ProtobufSerializer::serialize(const core::Message& msg) {
    if (msg.format() == core::MessageFormat::Protobuf) {
        return msg.bytes();
    }
#ifdef RYBENCH_ENABLE_FLATBUFFERS
    if (msg.format() == core::MessageFormat::FlatBuffers) {
        if (msg.empty()) {
            buffer_.clear();
            return buffer_;
        }

        const reflection::Schema* fbSchema = msg.schema();
        if (!fbSchema) {
            fbSchema = fbsSchema_.reflectionSchema();
        }
        if (!fbSchema || !fbSchema->root_table()) {
            throw std::runtime_error("No FlatBuffers schema available for conversion");
        }
        if (!rootDescriptor_) {
            throw std::runtime_error("No Protobuf descriptor available");
        }

        std::unique_ptr<google::protobuf::Message> protoMsg(
            factory_.GetPrototype(rootDescriptor_)->New());

        auto* fbTable = flatbuffers::GetAnyRoot(msg.data());
        populateFromFlatBuffer(protoMsg.get(), fbTable, fbSchema->root_table());

        const std::size_t n = static_cast<std::size_t>(protoMsg->ByteSizeLong());
        buffer_.resize(n);
        if (!protoMsg->SerializeToArray(buffer_.data(), static_cast<int>(n))) {
            buffer_.clear();
        }
        return buffer_;
    }
#endif
    throw std::runtime_error(
        std::string("Unsupported conversion: ")
        + std::string(core::toString(msg.format()))
        + " -> protobuf");
}

#ifdef RYBENCH_ENABLE_FLATBUFFERS
static std::string getSimpleName(const std::string& fullName) {
    auto pos = fullName.rfind('.');
    if (pos != std::string::npos) {
        return fullName.substr(pos + 1);
    }
    return fullName;
}

static std::string getPackageName(const std::string& fullName) {
    auto pos = fullName.rfind('.');
    if (pos != std::string::npos) {
        return fullName.substr(0, pos);
    }
    return "";
}

void ProtobufSerializer::buildDescriptorFromBfbs(const reflection::Schema* fbSchema) {
    google::protobuf::FileDescriptorProto fileProto;
    fileProto.set_name("generated_from_bfbs.proto");
    fileProto.set_syntax("proto3");

    if (!fbSchema->objects()) {
        throw std::runtime_error("FlatBuffers schema has no objects");
    }

    std::string packageName;
    if (fbSchema->root_table()) {
        packageName = getPackageName(fbSchema->root_table()->name()->str());
        if (!packageName.empty()) {
            fileProto.set_package(packageName);
        }
    }

    if (fbSchema->enums()) {
        for (const auto* fbEnum : *fbSchema->enums()) {
            auto* enumProto = fileProto.add_enum_type();
            enumProto->set_name(getSimpleName(fbEnum->name()->str()));

            if (fbEnum->values()) {
                for (const auto* val : *fbEnum->values()) {
                    auto* valueProto = enumProto->add_value();
                    valueProto->set_name(val->name()->str());
                    valueProto->set_number(static_cast<int>(val->value()));
                }
            }
        }
    }

    for (const auto* obj : *fbSchema->objects()) {
        auto* msgProto = fileProto.add_message_type();
        msgProto->set_name(getSimpleName(obj->name()->str()));

        if (obj->fields()) {
            for (const auto* field : *obj->fields()) {
                auto* fieldProto = msgProto->add_field();
                fieldProto->set_name(field->name()->str());
                fieldProto->set_number(field->id() + 1);

                auto baseType = field->type()->base_type();
                auto elemType = field->type()->element();

                if (baseType == reflection::BaseType::Vector) {
                    fieldProto->set_label(google::protobuf::FieldDescriptorProto::LABEL_REPEATED);

                    if (elemType == reflection::BaseType::Obj) {
                        fieldProto->set_type(google::protobuf::FieldDescriptorProto::TYPE_MESSAGE);
                        if (field->type()->index() >= 0) {
                            auto* nestedObj = fbSchema->objects()->Get(field->type()->index());
                            fieldProto->set_type_name(getSimpleName(nestedObj->name()->str()));
                        }
                    } else {
                        fieldProto->set_type(mapFbTypeToProtoType(elemType));
                    }
                } else if (baseType == reflection::BaseType::Obj) {
                    fieldProto->set_label(google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL);
                    fieldProto->set_type(google::protobuf::FieldDescriptorProto::TYPE_MESSAGE);
                    if (field->type()->index() >= 0) {
                        auto* nestedObj = fbSchema->objects()->Get(field->type()->index());
                        fieldProto->set_type_name(getSimpleName(nestedObj->name()->str()));
                    }
                } else if (field->type()->index() >= 0 && fbSchema->enums() &&
                           (baseType == reflection::BaseType::Byte ||
                            baseType == reflection::BaseType::UByte ||
                            baseType == reflection::BaseType::Short ||
                            baseType == reflection::BaseType::UShort ||
                            baseType == reflection::BaseType::Int ||
                            baseType == reflection::BaseType::UInt)) {
                    fieldProto->set_label(google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL);
                    fieldProto->set_type(google::protobuf::FieldDescriptorProto::TYPE_ENUM);
                    auto* enumDef = fbSchema->enums()->Get(field->type()->index());
                    fieldProto->set_type_name(getSimpleName(enumDef->name()->str()));
                } else {
                    fieldProto->set_label(google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL);
                    fieldProto->set_type(mapFbTypeToProtoType(baseType));
                }
            }
        }
    }

    const google::protobuf::FileDescriptor* fileDesc = pool_.BuildFile(fileProto);
    if (!fileDesc) {
        throw std::runtime_error("Failed to build Protobuf descriptor from FlatBuffers schema");
    }

    if (fbSchema->root_table()) {
        std::string rootName = fbSchema->root_table()->name()->str();
        rootDescriptor_ = pool_.FindMessageTypeByName(rootName);
        if (!rootDescriptor_) {
            throw std::runtime_error("Failed to find root message type: " + rootName);
        }
    }
}

google::protobuf::FieldDescriptorProto::Type ProtobufSerializer::mapFbTypeToProtoType(
    reflection::BaseType fbType) const {
    switch (fbType) {
        case reflection::BaseType::Bool:   return google::protobuf::FieldDescriptorProto::TYPE_BOOL;
        case reflection::BaseType::Byte:   return google::protobuf::FieldDescriptorProto::TYPE_INT32;
        case reflection::BaseType::UByte:  return google::protobuf::FieldDescriptorProto::TYPE_UINT32;
        case reflection::BaseType::Short:  return google::protobuf::FieldDescriptorProto::TYPE_INT32;
        case reflection::BaseType::UShort: return google::protobuf::FieldDescriptorProto::TYPE_UINT32;
        case reflection::BaseType::Int:    return google::protobuf::FieldDescriptorProto::TYPE_INT32;
        case reflection::BaseType::UInt:   return google::protobuf::FieldDescriptorProto::TYPE_UINT32;
        case reflection::BaseType::Long:   return google::protobuf::FieldDescriptorProto::TYPE_INT64;
        case reflection::BaseType::ULong:  return google::protobuf::FieldDescriptorProto::TYPE_UINT64;
        case reflection::BaseType::Float:  return google::protobuf::FieldDescriptorProto::TYPE_FLOAT;
        case reflection::BaseType::Double: return google::protobuf::FieldDescriptorProto::TYPE_DOUBLE;
        case reflection::BaseType::String: return google::protobuf::FieldDescriptorProto::TYPE_STRING;
        default:                           return google::protobuf::FieldDescriptorProto::TYPE_BYTES;
    }
}

google::protobuf::FieldDescriptorProto::Label ProtobufSerializer::getProtoLabel(
    const reflection::Field* field) const {
    if (field->type()->base_type() == reflection::BaseType::Vector) {
        return google::protobuf::FieldDescriptorProto::LABEL_REPEATED;
    }
    return google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL;
}

void ProtobufSerializer::populateFromFlatBuffer(google::protobuf::Message* protoMsg,
                                                 const flatbuffers::Table* fbTable,
                                                 const reflection::Object* fbObj) const {
    if (!protoMsg || !fbTable || !fbObj) return;

    const auto* protoDesc = protoMsg->GetDescriptor();
    const auto* protoRefl = protoMsg->GetReflection();
    const reflection::Schema* fbSchema = fbsSchema_.reflectionSchema();

    if (!fbObj->fields()) return;

    for (const auto* fbField : *fbObj->fields()) {
        std::string fieldName = fbField->name()->str();

        const auto* protoField = protoDesc->FindFieldByName(fieldName);
        if (!protoField) {
            std::string lowerName = fieldName;
            for (auto& c : lowerName) c = std::tolower(c);
            protoField = protoDesc->FindFieldByName(lowerName);
        }

        if (protoField) {
            setProtoFieldFromFlatBuffer(protoMsg, protoField, fbTable, fbField);
        }
    }
}

void ProtobufSerializer::setProtoFieldFromFlatBuffer(google::protobuf::Message* protoMsg,
                                                      const google::protobuf::FieldDescriptor* protoField,
                                                      const flatbuffers::Table* fbTable,
                                                      const reflection::Field* fbField) const {
    const auto* protoRefl = protoMsg->GetReflection();
    const reflection::Schema* fbSchema = fbsSchema_.reflectionSchema();
    auto fbBaseType = fbField->type()->base_type();

    if (protoField->is_repeated()) {
        if (fbBaseType == reflection::BaseType::Vector) {
            auto elemType = fbField->type()->element();

            if (elemType == reflection::BaseType::Obj) {
                auto* vec = fbTable->GetPointer<const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::Table>>*>(fbField->offset());
                if (vec && fbField->type()->index() >= 0 && fbSchema) {
                    auto* elemObj = fbSchema->objects()->Get(fbField->type()->index());
                    for (size_t i = 0; i < vec->size(); ++i) {
                        auto* nestedProto = protoRefl->AddMessage(protoMsg, protoField);
                        populateFromFlatBuffer(nestedProto, vec->Get(i), elemObj);
                    }
                }
            } else if (elemType == reflection::BaseType::String) {
                auto* vec = fbTable->GetPointer<const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>*>(fbField->offset());
                if (vec) {
                    for (size_t i = 0; i < vec->size(); ++i) {
                        protoRefl->AddString(protoMsg, protoField, vec->Get(i)->c_str());
                    }
                }
            } else {
                switch (elemType) {
                    case reflection::BaseType::Int: {
                        auto* vec = fbTable->GetPointer<const flatbuffers::Vector<int32_t>*>(fbField->offset());
                        if (vec) for (size_t i = 0; i < vec->size(); ++i) protoRefl->AddInt32(protoMsg, protoField, vec->Get(i));
                        break;
                    }
                    case reflection::BaseType::UInt: {
                        auto* vec = fbTable->GetPointer<const flatbuffers::Vector<uint32_t>*>(fbField->offset());
                        if (vec) for (size_t i = 0; i < vec->size(); ++i) protoRefl->AddUInt32(protoMsg, protoField, vec->Get(i));
                        break;
                    }
                    case reflection::BaseType::Long: {
                        auto* vec = fbTable->GetPointer<const flatbuffers::Vector<int64_t>*>(fbField->offset());
                        if (vec) for (size_t i = 0; i < vec->size(); ++i) protoRefl->AddInt64(protoMsg, protoField, vec->Get(i));
                        break;
                    }
                    case reflection::BaseType::ULong: {
                        auto* vec = fbTable->GetPointer<const flatbuffers::Vector<uint64_t>*>(fbField->offset());
                        if (vec) for (size_t i = 0; i < vec->size(); ++i) protoRefl->AddUInt64(protoMsg, protoField, vec->Get(i));
                        break;
                    }
                    case reflection::BaseType::Float: {
                        auto* vec = fbTable->GetPointer<const flatbuffers::Vector<float>*>(fbField->offset());
                        if (vec) for (size_t i = 0; i < vec->size(); ++i) protoRefl->AddFloat(protoMsg, protoField, vec->Get(i));
                        break;
                    }
                    case reflection::BaseType::Double: {
                        auto* vec = fbTable->GetPointer<const flatbuffers::Vector<double>*>(fbField->offset());
                        if (vec) for (size_t i = 0; i < vec->size(); ++i) protoRefl->AddDouble(protoMsg, protoField, vec->Get(i));
                        break;
                    }
                    default:
                        break;
                }
            }
        }
    } else {
        switch (fbBaseType) {
            case reflection::BaseType::Bool:
                protoRefl->SetBool(protoMsg, protoField, fbTable->GetField<uint8_t>(fbField->offset(), 0) != 0);
                break;
            case reflection::BaseType::Byte:
                if (protoField->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) {
                    protoRefl->SetEnumValue(protoMsg, protoField, fbTable->GetField<int8_t>(fbField->offset(), 0));
                } else {
                    protoRefl->SetInt32(protoMsg, protoField, fbTable->GetField<int8_t>(fbField->offset(), 0));
                }
                break;
            case reflection::BaseType::UByte:
                if (protoField->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) {
                    protoRefl->SetEnumValue(protoMsg, protoField, fbTable->GetField<uint8_t>(fbField->offset(), 0));
                } else {
                    protoRefl->SetUInt32(protoMsg, protoField, fbTable->GetField<uint8_t>(fbField->offset(), 0));
                }
                break;
            case reflection::BaseType::Short:
                protoRefl->SetInt32(protoMsg, protoField, fbTable->GetField<int16_t>(fbField->offset(), 0));
                break;
            case reflection::BaseType::UShort:
                protoRefl->SetUInt32(protoMsg, protoField, fbTable->GetField<uint16_t>(fbField->offset(), 0));
                break;
            case reflection::BaseType::Int:
                protoRefl->SetInt32(protoMsg, protoField, fbTable->GetField<int32_t>(fbField->offset(), 0));
                break;
            case reflection::BaseType::UInt:
                protoRefl->SetUInt32(protoMsg, protoField, fbTable->GetField<uint32_t>(fbField->offset(), 0));
                break;
            case reflection::BaseType::Long:
                protoRefl->SetInt64(protoMsg, protoField, fbTable->GetField<int64_t>(fbField->offset(), 0));
                break;
            case reflection::BaseType::ULong:
                protoRefl->SetUInt64(protoMsg, protoField, fbTable->GetField<uint64_t>(fbField->offset(), 0));
                break;
            case reflection::BaseType::Float:
                protoRefl->SetFloat(protoMsg, protoField, fbTable->GetField<float>(fbField->offset(), 0.0f));
                break;
            case reflection::BaseType::Double:
                protoRefl->SetDouble(protoMsg, protoField, fbTable->GetField<double>(fbField->offset(), 0.0));
                break;
            case reflection::BaseType::String: {
                auto* str = fbTable->GetPointer<const flatbuffers::String*>(fbField->offset());
                if (str) {
                    protoRefl->SetString(protoMsg, protoField, str->c_str());
                }
                break;
            }
            case reflection::BaseType::Obj: {
                auto* nestedTable = fbTable->GetPointer<const flatbuffers::Table*>(fbField->offset());
                if (nestedTable && fbField->type()->index() >= 0 && fbSchema) {
                    auto* nestedObj = fbSchema->objects()->Get(fbField->type()->index());
                    if (nestedObj) {
                        auto* nestedProto = protoRefl->MutableMessage(protoMsg, protoField);
                        populateFromFlatBuffer(nestedProto, nestedTable, nestedObj);
                    }
                }
                break;
            }
            default:
                break;
        }
    }
}
#endif // RYBENCH_ENABLE_FLATBUFFERS

void ProtobufSerializer::loadSchema(const core::SchemaDefinition&) {}
void ProtobufSerializer::initMessage() {}
void ProtobufSerializer::addBool  (const core::SchemaField&, bool) {}
void ProtobufSerializer::addInt   (const core::SchemaField&, int64_t) {}
void ProtobufSerializer::addUInt  (const core::SchemaField&, uint64_t) {}
void ProtobufSerializer::addFloat (const core::SchemaField&, float) {}
void ProtobufSerializer::addDouble(const core::SchemaField&, double) {}
void ProtobufSerializer::addString(const core::SchemaField&, std::string_view) {}
void ProtobufSerializer::beginTable (const core::SchemaField&) {}
void ProtobufSerializer::endTable   () {}
void ProtobufSerializer::beginVector(const core::SchemaField&, size_t) {}
void ProtobufSerializer::endVector  () {}
core::Message ProtobufSerializer::finalizeMessage() {
    throw std::runtime_error("ProtobufSerializer::finalizeMessage: builder API not yet implemented");
}

} // namespace rybench::serializer
