#pragma once

#include <rybench/serializer/serializer.hpp>
#include "flatbuffers_schema.hpp"
#include <flatbuffers/flatbuffers.h>
#include <string>
#include <vector>

namespace rybench::serializer {

class FlatbuffersSerializer : public Serializer {
public:
    explicit FlatbuffersSerializer(const std::string& bfbsPath = "");

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

private:
    struct PendingScalar {
        uint16_t        vtableOffset;
        core::FieldType type;
        int64_t         value;
        int64_t         defaultVal;
    };

    struct Context {
        uint16_t parentFieldId = 0;

        // Table mode
        std::vector<PendingScalar> scalars;
        std::vector<std::pair<uint16_t, flatbuffers::Offset<void>>> offsets;

        // Vector mode
        bool isVector = false;
        core::FieldType vectorElemType = core::FieldType::Unknown;
        std::vector<int64_t>                   vectorScalars;
        std::vector<flatbuffers::Offset<void>> vectorOffsets;
    };

    void writeTable (Context& ctx, bool isRoot);
    void writeVector(Context& ctx);

    core::FlatBuffersSchema        fbsSchema_;
    flatbuffers::FlatBufferBuilder builder_{1024};
    std::vector<Context>           stack_;
};

} // namespace rybench::serializer
