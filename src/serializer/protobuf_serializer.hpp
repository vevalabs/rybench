#pragma once

#include "serializer.hpp"
#include <google/protobuf/message.h>
#include <google/protobuf/descriptor.h>
#include <functional>
#include <unordered_map>
#include <memory>
#include <fstream>

namespace serializer {

class ProtobufSerializer : public ISerializer {
public:
    using MessagePtr = std::unique_ptr<google::protobuf::Message>;
    using MessageFactory = std::function<MessagePtr()>;

    explicit ProtobufSerializer(const std::string& messageName = "",
                                const std::string& fieldsConfigPath = "");

    std::vector<uint8_t> serialize(const common::DataValue& value) override;
    common::DataValue deserialize(const std::vector<uint8_t>& data) override;
    common::Schema getSchema() const override;
    std::string name() const override { return "protobuf"; }

    // Register a message type (called at startup for each compiled proto)
    template<typename T>
    static void registerMessage(const std::string& name) {
        registry()[name] = []() -> MessagePtr {
            return std::make_unique<T>();
        };
    }

    // Get available message types
    static std::vector<std::string> availableMessages();
    static std::string availableMessagesStr();

    // Get factory for a message type
    static MessageFactory getFactory(const std::string& name);

private:
    std::string messageName_;
    MessageFactory factory_;
    std::unordered_map<std::string, common::FieldConstraints> fieldConstraints_;

    static std::unordered_map<std::string, MessageFactory>& registry();

    void loadFieldsConfig(const std::string& path);
    common::SchemaField descriptorToSchemaField(
        const google::protobuf::FieldDescriptor* field,
        const std::string& path) const;
    common::FieldConstraints getConstraints(const std::string& path,
                                            const std::string& typeName) const;
};

// Macro for easy registration (use short name without namespace)
#define REGISTER_PROTO_MESSAGE(MessageType, ShortName) \
    namespace { \
        struct ShortName##Registrar { \
            ShortName##Registrar() { \
                serializer::ProtobufSerializer::registerMessage<MessageType>(#MessageType); \
            } \
        }; \
        static ShortName##Registrar g_##ShortName##Registrar; \
    }

} // namespace serializer
