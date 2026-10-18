#include "monpoly_serializer.hpp"
#include <stdexcept>
#ifdef RYBENCH_ENABLE_FLATBUFFERS
#include <flatbuffers/flatbuffers.h>
#endif
#include <iostream>

namespace rybench::serializer {

MonpolySerializer::MonpolySerializer(const std::string& bfbsPath) {
#ifdef RYBENCH_ENABLE_FLATBUFFERS
    if (!bfbsPath.empty()) {
        try {
            fbsSchema_.loadFromBfbs(bfbsPath);
        } catch (const std::exception& e) {
            std::cerr << "Warning: Failed to load bfbs schema: " << e.what() << std::endl;
        }
    }
#endif
}

std::string MonpolySerializer::name() const {
    return "monpoly";
}

core::MessageFormat MonpolySerializer::format() const {
    return core::MessageFormat::Monpoly;
}

std::span<const uint8_t> MonpolySerializer::serialize(const core::Message& msg) {
    if (msg.format() == core::MessageFormat::Monpoly) {
        return msg.bytes();
    }
#ifdef RYBENCH_ENABLE_FLATBUFFERS
    if (msg.format() == core::MessageFormat::FlatBuffers) {
        buffer_.clear();
        const reflection::Schema* schema = msg.schema();
        if (!schema) schema = fbsSchema_.reflectionSchema();
        if (schema && schema->root_table() && !msg.empty()) {
            const auto* table = flatbuffers::GetAnyRoot(msg.data());
            for (const auto* field : *schema->root_table()->fields()) {
                if (field->type()->base_type() == reflection::BaseType::Bool &&
                    flatbuffers::GetAnyFieldI(*table, *field) != 0) {
                    const auto& name = field->name()->str();
                    buffer_.insert(buffer_.end(), name.begin(), name.end());
                    static const char suffix[] = "()\n";
                    buffer_.insert(buffer_.end(), suffix, suffix + 3);
                }
            }
        }
        return buffer_;
    }
#endif
    throw std::runtime_error(
        std::string("Unsupported conversion: ")
        + std::string(core::toString(msg.format()))
        + " -> monpoly");
}

void MonpolySerializer::loadSchema(const core::SchemaDefinition&) {}
void MonpolySerializer::initMessage() {}
void MonpolySerializer::addBool  (const core::SchemaField&, bool) {}
void MonpolySerializer::addInt   (const core::SchemaField&, int64_t) {}
void MonpolySerializer::addUInt  (const core::SchemaField&, uint64_t) {}
void MonpolySerializer::addFloat (const core::SchemaField&, float) {}
void MonpolySerializer::addDouble(const core::SchemaField&, double) {}
void MonpolySerializer::addString(const core::SchemaField&, std::string_view) {}
void MonpolySerializer::beginTable (const core::SchemaField&) {}
void MonpolySerializer::endTable   () {}
void MonpolySerializer::beginVector(const core::SchemaField&, size_t) {}
void MonpolySerializer::endVector  () {}
core::Message MonpolySerializer::finalizeMessage() {
    throw std::runtime_error("MonpolySerializer::finalizeMessage: builder API not yet implemented");
}

} // namespace rybench::serializer
