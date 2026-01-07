#pragma once

#include "publisher.hpp"
#include "config.hpp"

namespace publisher {

PublisherConfig createPublisherConfig(const common::Config& config);
PublisherPtr createPublisher(const common::Config& config);

} // namespace publisher
