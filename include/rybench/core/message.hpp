#pragma once

#include <rybench/core/message_format.hpp>
#include <vector>
#include <cstdint>
#include <span>
#include <string>
#include <flatbuffers/reflection.h>

namespace rybench::core {

class Message {
public:
    Message() = default;
    explicit Message(std::vector<uint8_t> buffer);
    Message(std::vector<uint8_t> buffer, const reflection::Schema* schema);
    Message(std::vector<uint8_t> buffer, MessageFormat format);
    Message(std::vector<uint8_t> buffer, MessageFormat format, const reflection::Schema* schema);

    // Move semantics
    Message(Message&&) = default;
    Message& operator=(Message&&) = default;

    // Copy semantics
    Message(const Message&) = default;
    Message& operator=(const Message&) = default;

    // Format tag
    MessageFormat format() const { return format_; }

    // Raw buffer access
    const uint8_t* data() const { return buffer_.data(); }
    size_t size() const { return buffer_.size(); }
    std::span<const uint8_t> bytes() const { return { buffer_.data(), buffer_.size() }; }
    const std::vector<uint8_t>& buffer() const { return buffer_; }
    std::vector<uint8_t> release() { return std::move(buffer_); }

    // Check if message is empty
    bool empty() const { return buffer_.empty(); }

    // Schema reference for introspection (FlatBuffers-specific)
    void setSchema(const reflection::Schema* schema) { schema_ = schema; }
    const reflection::Schema* schema() const { return schema_; }

    // Validation using FlatBuffers verifier
    bool isValid() const;

    // Convert to JSON string for debugging (requires schema)
    std::string toJson() const;

private:
    std::vector<uint8_t> buffer_;
    MessageFormat format_ = MessageFormat::FlatBuffers;
    const reflection::Schema* schema_ = nullptr;
};

} // namespace rybench::core
