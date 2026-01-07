#pragma once

#include "publisher.hpp"
#include <zenoh.hxx>
#include <memory>
#include <string>

namespace publisher {

class ZenohPublisher : public IPublisher {
public:
    ZenohPublisher() = default;
    ~ZenohPublisher() override;

    void init(const PublisherConfig& config) override;
    bool publish(const void* data, size_t size) override;
    void shutdown() override;
    std::string name() const override;

private:
    PublisherConfig config_;
    std::string keyExpr_;
    std::unique_ptr<zenoh::Session> session_;
    std::unique_ptr<zenoh::Publisher> publisher_;
    bool initialized_ = false;
};

} // namespace publisher
