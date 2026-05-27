#pragma once

#include <rybench/generator/generator.hpp>
#include <rybench/common/config.hpp>
#include <expected>
#include <string>

namespace rybench {
namespace generator {

std::expected<GeneratorPtr, std::string> createGenerator(const common::Config& config);

} // namespace generator
} // namespace rybench
