#pragma once

#include <algorithm>
#include <stdint.h>

namespace u1 {

struct FilamentDisplayColor {
    uint32_t rgb;
    bool rainbow;
};

inline FilamentDisplayColor mapFilamentColor(uint32_t rgb) {
    constexpr uint8_t NearWhite = 232;
    static constexpr uint8_t MinimumColorPeak = 192;
    const auto red = static_cast<uint8_t>(rgb >> 16);
    const auto green = static_cast<uint8_t>(rgb >> 8);
    const auto blue = static_cast<uint8_t>(rgb);
    const uint8_t peak = std::max({red, green, blue});
    const uint8_t spread = peak - std::min({red, green, blue});

    // Allow small RGB differences in neutral swatches but keep tinted colors and whites.
    const bool neutral = spread <= 5 || (spread <= 14 && spread * 10 <= peak);
    if (neutral && peak < NearWhite) return {rgb, true};
    if (peak >= MinimumColorPeak) return {rgb, false};

    // Raise dim colors before the zone's brightness and effect are applied.
    const auto lift = [peak](uint8_t channel) -> uint32_t {
        return (channel * MinimumColorPeak + peak / 2) / peak;
    };
    return {(lift(red) << 16) | (lift(green) << 8) | lift(blue), false};
}

}  // namespace u1
