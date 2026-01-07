#pragma once

#include "generator.hpp"
#include "config.hpp"
#include "schema.hpp"

namespace generator {

GeneratorPtr createGenerator(const common::Config& config, const common::Schema& schema);

} // namespace generator
