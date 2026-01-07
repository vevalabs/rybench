#include "publisher_factory.hpp"
#include "iceoryx_publisher.hpp"
#ifdef HAVE_ZENOH
#include "zenoh_publisher.hpp"
#endif

#include <stdexcept>

namespace publisher {

PublisherConfig createPublisherConfig(const common::Config& config) {
    PublisherConfig pubConfig;
    pubConfig.serviceName = config.get("publisher", "service", "Benchmark");
    pubConfig.instanceName = config.get("publisher", "instance", "Instance");
    pubConfig.eventName = config.get("publisher", "event", "Event");
    pubConfig.maxPayloadSize = config.getInt("publisher", "max_payload_size", 4096);
    return pubConfig;
}

PublisherPtr createPublisher(const common::Config& config) {
    std::string type = config.get("publisher", "type", "iceoryx");

    PublisherPtr pub;

    if (type == "iceoryx") {
        std::string appName = config.get("runtime", "app_name", "publisher");
        IceoryxPublisher::initRuntime(appName);
        pub = std::make_unique<IceoryxPublisher>();
    }
#ifdef HAVE_ZENOH
    else if (type == "zenoh") {
        pub = std::make_unique<ZenohPublisher>();
    }
#endif
    else {
        std::string available = "iceoryx";
#ifdef HAVE_ZENOH
        available += ", zenoh";
#endif
        throw std::runtime_error("Unknown publisher type: " + type + ". Available: " + available);
    }

    auto pubConfig = createPublisherConfig(config);
    pub->init(pubConfig);

    return pub;
}

} // namespace publisher
