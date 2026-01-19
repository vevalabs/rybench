#pragma once

#include "data_value.hpp"
#include <fstream>
#include <string>

namespace rybench {
namespace common {

class Recorder {
public:
    Recorder() = default;
    ~Recorder();

    void open(const std::string& filepath);
    void record(const DataValue& value);
    void close();

    size_t count() const { return count_; }
    bool isOpen() const { return file_.is_open(); }

private:
    std::ofstream file_;
    size_t count_ = 0;
};

} // namespace common
} // namespace rybench
