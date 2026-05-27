#pragma once

#include <rybench/serializer/serializer.hpp>
#include <rybench/common/config.hpp>
#include <expected>
#include <string>

namespace rybench::serializer {

std::expected<SerializerPtr, std::string> createSerializer(const common::Config& config);

} // namespace rybench::serializer
