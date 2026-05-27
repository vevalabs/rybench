#pragma once

#include <rybench/generator/generator.hpp>
#include <rybench/common/config.hpp>

namespace rybench {
namespace generator {

GeneratorPtr createGenerator(const common::Config& config);

} // namespace generator
} // namespace rybench
