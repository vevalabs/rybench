#pragma once

#include <rybench/serializer/serializer.hpp>
#include <rybench/core/config.hpp>
#include <expected>
#include <string>

namespace rybench::serializer {

std::expected<SerializerPtr, std::string> createSerializer(const core::Config& config);

} // namespace rybench::serializer
