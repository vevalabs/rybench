#pragma once

#include "publisher.hpp"
#include "iceoryx_posh/popo/untyped_publisher.hpp"
#include "iceoryx_posh/runtime/posh_runtime.hpp"
#include <memory>
#include <string>

namespace rybench {
namespace publisher {

class IceoryxPublisher : public IPublisher {
public:
    static void initRuntime(const std::string& appName);

    IceoryxPublisher() = default;
    ~IceoryxPublisher() override;

    void init(const PublisherConfig& config) override;
    bool publish(const void* data, size_t size) override;
    void shutdown() override;
    std::string name() const override;

private:
    PublisherConfig config_;
    std::unique_ptr<iox::popo::UntypedPublisher> publisher_;
    bool initialized_ = false;
};

} // namespace publisher
} // namespace rybench
