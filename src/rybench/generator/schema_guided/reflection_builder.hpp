#pragma once

#include <rybench/core/flatbuffers_schema.hpp>
#include <rybench/core/message.hpp>
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/reflection.h>
#include <functional>
#include <string>

namespace rybench::generator {

class ReflectionBuilder {
public:
    explicit ReflectionBuilder(const core::FlatBuffersSchema& schema);

    struct Samplers {
        std::function<int64_t(const core::FbsFieldConstraints&, const std::string& path, bool isUnsigned)> sampleInt;
        std::function<double  (const core::FbsFieldConstraints&, const std::string& path)> sampleDouble;
        std::function<std::string(const core::FbsFieldConstraints&, const std::string& path)> sampleString;
        std::function<bool()> sampleBool;
        std::function<int64_t(const reflection::Enum*)> sampleEnum;
        std::function<size_t(size_t min, size_t max)> sampleVectorSize;
    };

    core::Message build(const Samplers& samplers);

private:
    const core::FlatBuffersSchema& schema_;
    const Samplers* samplers_ = nullptr;

    flatbuffers::Offset<void> buildTable(
        flatbuffers::FlatBufferBuilder& builder,
        const reflection::Object* table,
        const std::vector<core::FbsSchemaField>& fields,
        const std::string& path = "");

    flatbuffers::Offset<flatbuffers::String> buildString(
        flatbuffers::FlatBufferBuilder& builder,
        const core::FbsFieldConstraints& constraints,
        const std::string& path);

    flatbuffers::Offset<void> buildVector(
        flatbuffers::FlatBufferBuilder& builder,
        const reflection::Field* field,
        const core::FbsSchemaField& schemaField,
        const std::string& path);

    flatbuffers::Offset<void> buildStruct(
        flatbuffers::FlatBufferBuilder& builder,
        const reflection::Object* structDef,
        const std::vector<core::FbsSchemaField>& fields,
        const std::string& path);

    std::string fieldPath(const std::string& basePath, const std::string& fieldName) const;
};

} // namespace rybench::generator
