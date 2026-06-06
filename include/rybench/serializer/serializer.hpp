#pragma once

#include <rybench/core/message.hpp>
#include <rybench/core/schema.hpp>
#include <span>
#include <string_view>
#include <cstdint>
#include <memory>
#include <string>

namespace rybench::serializer {

class Serializer {
public:
    virtual ~Serializer() = default;

    virtual std::string name() const = 0;
    virtual core::MessageFormat format() const = 0;

    // Field-by-field builder API — generator calls these while walking the schema
    virtual void loadSchema(const core::SchemaDefinition& schema) = 0;
    virtual void initMessage() = 0;
    virtual void addBool  (const core::SchemaField& f, bool val) = 0;
    virtual void addInt   (const core::SchemaField& f, int64_t val) = 0;
    virtual void addUInt  (const core::SchemaField& f, uint64_t val) = 0;
    virtual void addFloat (const core::SchemaField& f, float val) = 0;
    virtual void addDouble(const core::SchemaField& f, double val) = 0;
    virtual void addString(const core::SchemaField& f, std::string_view val) = 0;
    virtual void beginTable (const core::SchemaField& f) = 0;
    virtual void endTable   () = 0;
    virtual void beginVector(const core::SchemaField& f, size_t count) = 0;
    virtual void endVector  () = 0;
    virtual core::Message finalizeMessage() = 0;

    // Pass-through serialization for fixed/replay messages
    virtual std::span<const uint8_t> serialize(const core::Message& msg) = 0;

    // Schema parsed at construction time (nullptr if none loaded)
    virtual const core::SchemaDefinition* loadedSchema() const { return nullptr; }
};

using SerializerPtr = std::unique_ptr<Serializer>;

} // namespace rybench::serializer
