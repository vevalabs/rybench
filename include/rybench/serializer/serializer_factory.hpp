#pragma once

#include <rybench/serializer/serializer.hpp>
#include <rybench/common/config.hpp>

namespace rybench {
namespace serializer {

SerializerPtr createSerializer(const common::Config& config);

} // namespace serializer
} // namespace rybench
