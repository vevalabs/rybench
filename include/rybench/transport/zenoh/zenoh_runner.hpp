#pragma once
#include <rybench/common/config.hpp>
#include <atomic>

namespace rybench::transport::zenoh {

int run(const common::Config& config, std::atomic<bool>& running);

} // namespace rybench::transport::zenoh
