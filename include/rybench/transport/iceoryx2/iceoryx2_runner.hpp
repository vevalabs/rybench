#pragma once
#include <rybench/common/config.hpp>
#include <atomic>

namespace rybench::transport::iceoryx2 {

int run(const common::Config& config, std::atomic<bool>& running);

} // namespace rybench::transport::iceoryx2
