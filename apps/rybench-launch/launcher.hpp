#pragma once
#include <string>
#include <vector>

namespace rybench::launch {

int launch(const std::string& configRoot,
           const std::string& binDir,
           const std::vector<std::string>& nodeNames);

} // namespace rybench::launch
