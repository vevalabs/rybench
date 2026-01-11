#include "include/property_guided_generator.hpp"
#include <chrono>

namespace generator {

PropertyGuidedGenerator::PropertyGuidedGenerator()
    : rng_(std::chrono::steady_clock::now().time_since_epoch().count()) {}

void PropertyGuidedGenerator::loadSchema(const common::FlatBuffersSchema& schema) {
    schema_ = &schema;
    builder_ = std::make_unique<ReflectionBuilder>(schema);
}

void PropertyGuidedGenerator::setIntProperty(
    const std::string& fieldPath,
    std::function<int64_t(std::mt19937_64&)> gen) {
    if (builder_) {
        builder_->setIntGenerator(fieldPath, std::move(gen));
    }
}

void PropertyGuidedGenerator::setDoubleProperty(
    const std::string& fieldPath,
    std::function<double(std::mt19937_64&)> gen) {
    if (builder_) {
        builder_->setDoubleGenerator(fieldPath, std::move(gen));
    }
}

void PropertyGuidedGenerator::setStringProperty(
    const std::string& fieldPath,
    std::function<std::string(std::mt19937_64&)> gen) {
    if (builder_) {
        builder_->setStringGenerator(fieldPath, std::move(gen));
    }
}

void PropertyGuidedGenerator::setCounter(const std::string& fieldPath, int64_t start) {
    if (builder_) {
        builder_->setCounter(fieldPath, start);
    }
}

void PropertyGuidedGenerator::setTimestamp(const std::string& fieldPath) {
    if (builder_) {
        builder_->setTimestamp(fieldPath);
    }
}

void PropertyGuidedGenerator::setFixedValue(const std::string& fieldPath, int64_t value) {
    if (builder_) {
        builder_->setFixedValue(fieldPath, value);
    }
}

void PropertyGuidedGenerator::setFixedValue(const std::string& fieldPath, double value) {
    if (builder_) {
        builder_->setFixedValue(fieldPath, value);
    }
}

void PropertyGuidedGenerator::setFixedValue(const std::string& fieldPath, const std::string& value) {
    if (builder_) {
        builder_->setFixedValue(fieldPath, value);
    }
}

common::Message PropertyGuidedGenerator::generate() {
    if (!builder_) {
        return common::Message();
    }
    return builder_->build(rng_);
}

Algorithm PropertyGuidedGenerator::algorithm() const {
    return Algorithm::PropertyGuided;
}

std::string PropertyGuidedGenerator::name() const {
    return "property_guided";
}

} // namespace generator
