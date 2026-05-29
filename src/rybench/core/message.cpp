#include <rybench/core/message.hpp>
#include <flatbuffers/flatbuffers.h>

namespace rybench::core {

Message::Message(std::vector<uint8_t> buffer)
    : buffer_(std::move(buffer)) {}

Message::Message(std::vector<uint8_t> buffer, const reflection::Schema* schema)
    : buffer_(std::move(buffer)), schema_(schema) {}

Message::Message(std::vector<uint8_t> buffer, MessageFormat format)
    : buffer_(std::move(buffer)), format_(format) {}

Message::Message(std::vector<uint8_t> buffer, MessageFormat format, const reflection::Schema* schema)
    : buffer_(std::move(buffer)), format_(format), schema_(schema) {}

bool Message::isValid() const {
    if (buffer_.empty()) {
        return false;
    }

    if (format_ != MessageFormat::FlatBuffers) {
        return true;
    }

    if (buffer_.size() < sizeof(flatbuffers::uoffset_t)) {
        return false;
    }

    auto root_offset = flatbuffers::ReadScalar<flatbuffers::uoffset_t>(buffer_.data());
    if (root_offset >= buffer_.size()) {
        return false;
    }

    if (schema_ && schema_->root_table()) {
        return flatbuffers::Verify(*schema_, *schema_->root_table(),
                                   buffer_.data(), buffer_.size());
    }

    return true;
}

std::string Message::toJson() const {
    if (buffer_.empty()) {
        return "{}";
    }

    if (!schema_ || !schema_->root_table()) {
        return "{\"error\": \"no schema available\"}";
    }

    // TODO: Implement JSON conversion using reflection
    // For now, return a placeholder with size info
    return "{\"size\": " + std::to_string(buffer_.size()) + "}";
}

} // namespace rybench::core
