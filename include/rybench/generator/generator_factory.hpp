#pragma once

#include <rybench/generator/generator.hpp>
#include <rybench/core/config.hpp>
#include <rybench/serializer/serializer.hpp>
#include <expected>
#include <string>

namespace rybench::generator {

std::expected<GeneratorPtr, std::string> createGenerator(const core::Config& config,
                                                          serializer::Serializer& generationSerializer);

} // namespace rybench::generator
