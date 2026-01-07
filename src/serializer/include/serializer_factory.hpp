#pragma once

#include "serializer.hpp"
#include "config.hpp"

namespace serializer {

SerializerPtr createSerializer(const common::Config& config);

} // namespace serializer
