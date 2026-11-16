// monpoly-echo -- a stand-in for the monpoly binary that does no monitoring: it
// speaks MonPoly's -verbose stdin/stdout protocol and answers "true" for every
// timepoint the moment its block closes. Pointing MonpolySink at it measures the
// pipe + process-scheduling floor that any external-process monitor pays, so it can
// be subtracted from MonPoly's own numbers. -sig/-formula/-verbose are ignored.

#include <iostream>
#include <string>

int main() {
    std::ios::sync_with_stdio(false);
    std::cout << "monpoly-echo: every timepoint true" << std::endl;  // banner MonpolySink waits for

    auto emit = [](long k) {
        std::cout << "At time point " << k << ":\n"
                  << '@' << k << ". (time point " << k << "): true" << std::endl;
    };

    std::string line;
    long open = -1;   // timepoint whose block is open (its facts may still follow)
    while (std::getline(std::cin, line)) {
        if (line.empty() || line[0] != '@') continue;   // fact lines: ignored
        if (open >= 0) emit(open);                      // next header closes the open block
        open = std::stol(line.substr(1));
    }
    if (open >= 0) emit(open);                          // EOF closes the last block
}
