#pragma once

#include <algorithm>
#include <cstdint>

// Desktop Duplication uses BGRA byte order (blue, green, red, alpha).
struct BgraPixel {
    std::uint8_t b;
    std::uint8_t g;
    std::uint8_t r;
    std::uint8_t a;
};

inline bool IsWhitePixel(const BgraPixel& pixel, int threshold, int channelTolerance) noexcept {
    const int minimum = std::min({ static_cast<int>(pixel.r), static_cast<int>(pixel.g), static_cast<int>(pixel.b) });
    const int maximum = std::max({ static_cast<int>(pixel.r), static_cast<int>(pixel.g), static_cast<int>(pixel.b) });
    return minimum >= threshold && maximum - minimum <= channelTolerance;
}

inline bool IsRedPixel(const BgraPixel& pixel, int threshold, float ratio) noexcept {
    return pixel.r >= threshold
        && static_cast<float>(pixel.r) >= static_cast<float>(pixel.g) * ratio
        && static_cast<float>(pixel.r) >= static_cast<float>(pixel.b) * ratio;
}
