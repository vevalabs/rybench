#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <flatbuffers/reflection.h>

namespace rybench::common {

class Message {
public:
    Message() = default;
    explicit Message(std::vector<uint8_t> buffer);
    Message(std::vector<uint8_t> buffer, const reflection::Schema* schema);

    // Move semantics
    Message(Message&&) = default;
    Message& operator=(Message&&) = default;

    // Copy semantics
    Message(const Message&) = default;
    Message& operator=(const Message&) = default;

    // Raw buffer access
    const uint8_t* data() const { return buffer_.data(); }
    size_t size() const { return buffer_.size(); }
    const std::vector<uint8_t>& buffer() const { return buffer_; }
    std::vector<uint8_t> release() { return std::move(buffer_); }

    // Check if message is empty
    bool empty() const { return buffer_.empty(); }

    // Schema reference for introspection
    void setSchema(const reflection::Schema* schema) { schema_ = schema; }
    const reflection::Schema* schema() const { return schema_; }

    // Validation using FlatBuffers verifier
    bool isValid() const;

    // Convert to JSON string for debugging (requires schema)
    std::string toJson() const;

private:
    std::vector<uint8_t> buffer_;
    const reflection::Schema* schema_ = nullptr;
};

} // namespace rybench::common
