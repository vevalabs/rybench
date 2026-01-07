#pragma once

#include "serializer.hpp"
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/reflection.h>
#include <functional>
#include <unordered_map>
#include <memory>
#include <stdexcept>
#include <fstream>

namespace serializer {

class FlatbuffersSerializer : public ISerializer {
public:
    using BuilderFunc = std::function<std::vector<uint8_t>(const common::DataValue&)>;
    using ParserFunc = std::function<common::DataValue(const std::vector<uint8_t>&)>;

    struct MessageHandler {
        BuilderFunc build;
        ParserFunc parse;
    };

    explicit FlatbuffersSerializer(const std::string& messageName = "",
                                   const std::string& bfbsPath = "");

    std::vector<uint8_t> serialize(const common::DataValue& value) override;
    common::DataValue deserialize(const std::vector<uint8_t>& data) override;
    common::Schema getSchema() const override;
    std::string name() const override { return "flatbuffers"; }

    // Registry API
    static void registerMessage(const std::string& name, BuilderFunc builder, ParserFunc parser) {
        registry()[name] = {std::move(builder), std::move(parser)};
    }

    static std::vector<std::string> availableMessages();
    static std::string availableMessagesStr();

private:
    std::string messageName_;
    const MessageHandler* handler_ = nullptr;
    std::vector<uint8_t> bfbsData_;  // Binary schema data
    common::Schema schema_;

    static std::unordered_map<std::string, MessageHandler>& registry();
    void loadBinarySchema(const std::string& path);
    common::SchemaField reflectionFieldToSchema(const reflection::Field* field,
                                                 const reflection::Schema* schema) const;
};

// Macro for registering FlatBuffers messages - uses __LINE__ for namespaced types
#define REGISTER_FLATBUFFERS_MESSAGE_IMPL2(MessageName, BuilderFn, ParserFn, Line) \
    namespace { \
        struct FbsRegistrar##Line { \
            FbsRegistrar##Line() { \
                serializer::FlatbuffersSerializer::registerMessage( \
                    #MessageName, BuilderFn, ParserFn); \
            } \
        }; \
        static FbsRegistrar##Line g_fbsRegistrar##Line; \
    }

#define REGISTER_FLATBUFFERS_MESSAGE_IMPL(MessageName, BuilderFn, ParserFn, Line) \
    REGISTER_FLATBUFFERS_MESSAGE_IMPL2(MessageName, BuilderFn, ParserFn, Line)

#define REGISTER_FLATBUFFERS_MESSAGE(MessageName, BuilderFn, ParserFn) \
    REGISTER_FLATBUFFERS_MESSAGE_IMPL(MessageName, BuilderFn, ParserFn, __LINE__)

} // namespace serializer
