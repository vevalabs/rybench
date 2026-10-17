#include "protobuf_serializer.hpp"
#include <stdexcept>
#include <cctype>
#include <iostream>
#include <algorithm>
#include <cstring>

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

// ── direct build: field API → protobuf wire bytes ───────────────────────────────
//
// The generator drives this exactly like the FlatbuffersSerializer: initMessage,
// then addX / beginTable+endTable / beginVector(+element)+endVector, then
// finalizeMessage. We encode the protobuf wire format directly — no intermediate
// DynamicMessage, no reflection setters, no second serialize pass. Each message
// frame buffers its fields in walk order and, on close, emits them in field-number
// order (canonical) so the output matches google::protobuf byte-for-byte.

namespace {

using GpField = google::protobuf::FieldDescriptor;

// LEB128 base-128 varint.
inline void putVarint(std::vector<uint8_t>& b, uint64_t v) {
    while (v >= 0x80) { b.push_back(static_cast<uint8_t>(v) | 0x80); v >>= 7; }
    b.push_back(static_cast<uint8_t>(v));
}
inline void putFixed32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) { b.push_back(static_cast<uint8_t>(v)); v >>= 8; }
}
inline void putFixed64(std::vector<uint8_t>& b, uint64_t v) {
    for (int i = 0; i < 8; ++i) { b.push_back(static_cast<uint8_t>(v)); v >>= 8; }
}

// Wire type carried in the low 3 bits of a field tag.
inline uint32_t wireTypeFor(GpField::Type t) {
    switch (t) {
        case GpField::TYPE_DOUBLE:                       return 1;  // 64-bit
        case GpField::TYPE_FLOAT:                        return 5;  // 32-bit
        case GpField::TYPE_STRING:  case GpField::TYPE_BYTES:
        case GpField::TYPE_MESSAGE:                      return 2;  // length-delimited
        default:                                         return 0;  // varint
    }
}
inline void putTag(std::vector<uint8_t>& b, uint32_t number, uint32_t wt) {
    putVarint(b, (static_cast<uint64_t>(number) << 3) | wt);
}

// Append the raw wire value (no tag) for an integer-ish field, casting to the
// proto field's declared type. Only the types mapFbTypeToProtoType can emit appear.
inline void putIntValue(std::vector<uint8_t>& b, GpField::Type t, int64_t v) {
    switch (t) {
        case GpField::TYPE_INT32:
        case GpField::TYPE_ENUM:   putVarint(b, static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(v)))); break;
        case GpField::TYPE_INT64:  putVarint(b, static_cast<uint64_t>(v)); break;
        case GpField::TYPE_UINT32: putVarint(b, static_cast<uint64_t>(static_cast<uint32_t>(v))); break;
        case GpField::TYPE_UINT64: putVarint(b, static_cast<uint64_t>(v)); break;
        case GpField::TYPE_BOOL:   putVarint(b, v ? 1u : 0u); break;
        case GpField::TYPE_FLOAT:  { float  f = static_cast<float >(v); uint32_t u; std::memcpy(&u, &f, 4); putFixed32(b, u); break; }
        case GpField::TYPE_DOUBLE: { double d = static_cast<double>(v); uint64_t u; std::memcpy(&u, &d, 8); putFixed64(b, u); break; }
        default: break;
    }
}
// Append the raw wire value for a real-valued field (addFloat/addDouble).
inline void putRealValue(std::vector<uint8_t>& b, GpField::Type t, double v) {
    switch (t) {
        case GpField::TYPE_FLOAT:  { float f = static_cast<float>(v); uint32_t u; std::memcpy(&u, &f, 4); putFixed32(b, u); break; }
        case GpField::TYPE_DOUBLE: { uint64_t u; std::memcpy(&u, &v, 8); putFixed64(b, u); break; }
        case GpField::TYPE_INT32:
        case GpField::TYPE_ENUM:   putVarint(b, static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(v)))); break;
        case GpField::TYPE_INT64:  putVarint(b, static_cast<uint64_t>(static_cast<int64_t>(v))); break;
        case GpField::TYPE_UINT32: putVarint(b, static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int64_t>(v)))); break;
        case GpField::TYPE_UINT64: putVarint(b, static_cast<uint64_t>(static_cast<int64_t>(v))); break;
        default: break;
    }
}

} // namespace

// Emit a singular field instance (tag + value) into `fr.body`, recording it for the
// canonical (field-number-ordered) flush on close.
void ProtobufSerializer::emitInt(BuildFrame& fr, const GpField* fd, int64_t val) {
    uint32_t off = static_cast<uint32_t>(fr.body.size());
    putTag(fr.body, fd->number(), wireTypeFor(fd->type()));
    putIntValue(fr.body, fd->type(), val);
    fr.recs.push_back({ static_cast<uint32_t>(fd->number()), off,
                        static_cast<uint32_t>(fr.body.size() - off) });
}
void ProtobufSerializer::emitReal(BuildFrame& fr, const GpField* fd, double val) {
    uint32_t off = static_cast<uint32_t>(fr.body.size());
    putTag(fr.body, fd->number(), wireTypeFor(fd->type()));
    putRealValue(fr.body, fd->type(), val);
    fr.recs.push_back({ static_cast<uint32_t>(fd->number()), off,
                        static_cast<uint32_t>(fr.body.size() - off) });
}
void ProtobufSerializer::emitString(BuildFrame& fr, const GpField* fd, std::string_view val) {
    uint32_t off = static_cast<uint32_t>(fr.body.size());
    putTag(fr.body, fd->number(), 2);
    putVarint(fr.body, val.size());
    fr.body.insert(fr.body.end(), val.begin(), val.end());
    fr.recs.push_back({ static_cast<uint32_t>(fd->number()), off,
                        static_cast<uint32_t>(fr.body.size() - off) });
}

void ProtobufSerializer::initMessage() {
    depth_ = 0;
    if (!rootDescriptor_) return;
    BuildFrame& f = pushFrame();
    f.desc = rootDescriptor_;
}

void ProtobufSerializer::addBool(const core::SchemaField& f, bool val) {
    addInt(f, val ? 1 : 0);
}

void ProtobufSerializer::addInt(const core::SchemaField& f, int64_t val) {
    if (depth_ == 0) return;
    BuildFrame& fr = top();
    if (fr.isVector) {                                   // numeric vector element
        if (fr.number == 0) return;
        if (!fr.packed) putTag(fr.body, fr.number, wireTypeFor(fr.elemType));
        putIntValue(fr.body, fr.elemType, val);
        return;
    }
    if (val == 0) return;                                // proto3 singular default omitted
    const auto* fd = fr.desc ? fr.desc->FindFieldByName(f.name) : nullptr;
    if (fd) emitInt(fr, fd, val);
}

void ProtobufSerializer::addUInt(const core::SchemaField& f, uint64_t val) {
    addInt(f, static_cast<int64_t>(val));   // bit-preserved; putIntValue re-casts per field type
}

void ProtobufSerializer::addFloat(const core::SchemaField& f, float val) {
    addDouble(f, static_cast<double>(val));
}

void ProtobufSerializer::addDouble(const core::SchemaField& f, double val) {
    if (depth_ == 0) return;
    BuildFrame& fr = top();
    if (fr.isVector) {                                   // real vector element
        if (fr.number == 0) return;
        if (!fr.packed) putTag(fr.body, fr.number, wireTypeFor(fr.elemType));
        putRealValue(fr.body, fr.elemType, val);
        return;
    }
    if (val == 0.0) return;                              // proto3 singular default omitted
    const auto* fd = fr.desc ? fr.desc->FindFieldByName(f.name) : nullptr;
    if (fd) emitReal(fr, fd, val);
}

void ProtobufSerializer::addString(const core::SchemaField& f, std::string_view val) {
    if (depth_ == 0) return;
    BuildFrame& fr = top();
    if (fr.isVector) {                                   // string vector element (unpacked)
        if (fr.number == 0) return;
        putTag(fr.body, fr.number, 2);
        putVarint(fr.body, val.size());
        fr.body.insert(fr.body.end(), val.begin(), val.end());
        return;
    }
    if (val.empty()) return;                             // proto3 singular default omitted
    const auto* fd = fr.desc ? fr.desc->FindFieldByName(f.name) : nullptr;
    if (fd && fd->type() == GpField::TYPE_STRING) emitString(fr, fd, val);
}

void ProtobufSerializer::beginTable(const core::SchemaField& f) {
    const google::protobuf::Descriptor* childDesc = nullptr;
    uint32_t number = 0;
    if (depth_ > 0) {
        BuildFrame& fr = top();
        if (fr.isVector) {                               // vector-of-message element
            childDesc = fr.elemDesc;
            number    = fr.number;                       // wraps under the vector's field number
        } else if (fr.desc) {
            const auto* fd = fr.desc->FindFieldByName(f.name);
            if (fd && fd->cpp_type() == GpField::CPPTYPE_MESSAGE) {
                childDesc = fd->message_type();
                number    = static_cast<uint32_t>(fd->number());
            }
        }
    }
    BuildFrame& c = pushFrame();                          // pushed even if null, to balance endTable
    c.desc   = childDesc;
    c.number = number;
}

void ProtobufSerializer::endTable() {
    if (depth_ == 0) return;
    BuildFrame& c = top();
    // Canonical order: sort this message's fields by number, then embed the payload
    // as a length-delimited field in the parent.
    std::sort(c.recs.begin(), c.recs.end(),
              [](const BuildFrame::Rec& a, const BuildFrame::Rec& b) { return a.number < b.number; });
    uint32_t total = 0;
    for (const auto& r : c.recs) total += r.len;

    if (depth_ >= 2 && c.number != 0) {
        BuildFrame& parent = frames_[depth_ - 2];
        std::vector<uint8_t>& out = parent.body;
        uint32_t off = static_cast<uint32_t>(out.size());
        putTag(out, c.number, 2);
        putVarint(out, total);
        for (const auto& r : c.recs)
            out.insert(out.end(), c.body.begin() + r.off, c.body.begin() + r.off + r.len);
        if (!parent.isVector)                            // singular message field → a record; vector element → not
            parent.recs.push_back({ c.number, off, static_cast<uint32_t>(out.size() - off) });
    }
    --depth_;
}

void ProtobufSerializer::beginVector(const core::SchemaField& f, size_t) {
    const google::protobuf::FieldDescriptor* fd = nullptr;
    if (depth_ > 0 && top().desc)
        fd = top().desc->FindFieldByName(f.name);
    BuildFrame& v = pushFrame();
    v.isVector = true;
    if (fd && fd->is_repeated()) {
        v.number   = static_cast<uint32_t>(fd->number());
        v.elemType = fd->type();
        v.packed   = fd->is_packed();
        if (fd->cpp_type() == GpField::CPPTYPE_MESSAGE)
            v.elemDesc = fd->message_type();
    }
}

void ProtobufSerializer::endVector() {
    if (depth_ == 0) return;
    BuildFrame& v = top();
    if (depth_ >= 2 && v.number != 0 && !v.body.empty()) {
        BuildFrame& parent = frames_[depth_ - 2];
        uint32_t off = static_cast<uint32_t>(parent.body.size());
        if (v.packed) {                                  // packed scalars: one length-delimited field
            putTag(parent.body, v.number, 2);
            putVarint(parent.body, v.body.size());
        }
        // Unpacked (string/message): v.body already holds full tag+len+payload entries.
        parent.body.insert(parent.body.end(), v.body.begin(), v.body.end());
        parent.recs.push_back({ v.number, off, static_cast<uint32_t>(parent.body.size() - off) });
    }
    --depth_;
}

core::Message ProtobufSerializer::finalizeMessage() {
    if (depth_ == 0) return core::Message();
    BuildFrame& root = frames_[0];
    std::sort(root.recs.begin(), root.recs.end(),
              [](const BuildFrame::Rec& a, const BuildFrame::Rec& b) { return a.number < b.number; });
    std::vector<uint8_t> out;
    uint32_t total = 0;
    for (const auto& r : root.recs) total += r.len;
    out.reserve(total);
    for (const auto& r : root.recs)
        out.insert(out.end(), root.body.begin() + r.off, root.body.begin() + r.off + r.len);
    depth_ = 0;
    return core::Message(std::move(out), core::MessageFormat::Protobuf);
}

} // namespace rybench::serializer
