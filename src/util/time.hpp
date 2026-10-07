#pragma once

#include <chrono>

namespace util {

inline double time_now() {
    using clock = std::chrono::steady_clock;
    const auto tp = clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::duration<double>>(tp.time_since_epoch());
    return ms.count();
}

}