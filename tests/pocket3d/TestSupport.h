#ifndef POCKET3D_TEST_SUPPORT_H
#define POCKET3D_TEST_SUPPORT_H

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

class Pocket3DTestContext {
public:
    void Expect(bool condition, const std::string &message) {
        if (condition) return;
        ++failure_count_;
        std::cerr << "FAILED: " << message << std::endl;
    }

    void ExpectNear(float actual, float expected, float tolerance,
                    const std::string &message) {
        const bool finite = std::isfinite(actual) && std::isfinite(expected);
        Expect(finite && std::fabs(actual - expected) <= tolerance,
               message + " (expected " + std::to_string(expected) +
                   ", got " + std::to_string(actual) + ")");
    }

    int FailureCount() const { return failure_count_; }

private:
    int failure_count_ = 0;
};

inline std::string ReadPocket3DTestFile(const std::string &path) {
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input.is_open()) return "";
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

#endif
