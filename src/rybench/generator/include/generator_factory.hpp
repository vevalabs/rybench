#pragma once

#include "generator.hpp"
#include "config.hpp"

namespace rybench {
namespace generator {

GeneratorPtr createGenerator(const common::Config& config);

} // namespace generator
} // namespace rybench
