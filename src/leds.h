#pragma once

#include "moonraker.h"
#include "settings.h"
#include <Adafruit_NeoPixel.h>

namespace u1 {

struct Rgbw {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t w;
};

bool filamentColor(const LedSettings& config, const PrinterStatus& printer, uint32_t& rgb);

class Leds {
public:
    void begin(const LedSettings& config);
    void tick(uint32_t now, const LedSettings& config, const PrinterStatus& printer);
    uint32_t previewRgb(uint8_t pixel) const;
    const LedBoundaries& boundaries() const { return boundaries_; }
    void setLayout(const LedBoundaries& boundaries);
    void previewLayout(const LedBoundaries& boundaries, uint32_t now);

private:
    struct RgbAnimation {
        Rgbw from;
        Rgbw to;
        uint32_t blendStart;
        float phase;
    };

    Adafruit_NeoPixel strip_{PixelCount, U1_LED_GPIO, NEO_GRBW + NEO_KHZ800};
    uint32_t frame_[PixelCount]{};
    uint32_t lastFrame_ = 0;
    RgbAnimation animations_[ZoneCount]{};
    LedBoundaries boundaries_{};
    uint32_t rainbowElapsed_ = 0;
    bool showingLayout_ = false;
    uint32_t layoutStarted_ = 0;
    PrinterState state_ = PrinterState::Offline;
};

}  // namespace u1
