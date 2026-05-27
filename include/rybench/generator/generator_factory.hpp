#pragma once

#include <rybench/generator/generator.hpp>
#include <rybench/core/config.hpp>
#include <expected>
#include <string>

namespace rybench::generator {

std::expected<GeneratorPtr, std::string> createGenerator(const core::Config& config);

} // namespace rybench::generator
