#include "leds.h"
#include "filament_color.h"
#include <math.h>

static_assert(U1_LED_GPIO != 21, "GPIO21 is connected to the on-board RGB LED.");

namespace u1 {
namespace {

constexpr uint32_t LayoutPreviewMs = 1400;
constexpr uint32_t LayoutColors[ZoneCount] = {0x53E3CA, 0xF184AD, 0x79AEFF, 0xFFCA70};
constexpr uint32_t FrameInterval = 33;
constexpr float BlendMs = 420;
constexpr uint32_t RainbowPeriodMs = 3200;
constexpr uint32_t HueSteps = 65536;

float clamp01(float value) {
    return fmaxf(0.0f, fminf(1.0f, value));
}

float cycle(float value) {
    return value - floorf(value);
}

float wave(float phase) {
    return 0.5f - 0.5f * cosf(6.2831853f * phase);
}

float zoneGain(const ZoneSettings& settings, uint8_t limit) {
    return settings.enabled ? settings.brightness * limit / 10000.0f : 0.0f;
}

uint8_t scaled(uint8_t channel, float gain) {
    return static_cast<uint8_t>(channel * gain + 0.5f);
}

uint32_t pack(Rgbw color, float gain = 1.0f) {
    return Adafruit_NeoPixel::Color(scaled(color.r, gain), scaled(color.g, gain),
                                  scaled(color.b, gain), scaled(color.w, gain));
}

uint8_t mix(uint8_t first, uint8_t second, float factor) {
    return static_cast<uint8_t>(first + (second - first) * factor + 0.5f);
}

Rgbw blend(Rgbw first, Rgbw second, uint32_t elapsed) {
    const float x = clamp01(elapsed / BlendMs);
    const float factor = x * x * (3.0f - 2.0f * x);
    return {mix(first.r, second.r, factor), mix(first.g, second.g, factor),
            mix(first.b, second.b, factor), mix(first.w, second.w, factor)};
}

Rgbw unpackRgb(uint32_t rgb) {
    return {static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8),
            static_cast<uint8_t>(rgb), 0};
}

Rgbw stateColor(PrinterState state) {
    switch (state) {
        case PrinterState::Printing:  return {38, 96, 152, 0};
        case PrinterState::Standby:   return {18, 58, 126, 0};
        case PrinterState::Paused:    return {245, 142, 12, 0};
        case PrinterState::Complete:  return {20, 210, 64, 0};
        case PrinterState::Cancelled: return {245, 65, 3, 0};
        case PrinterState::Error:     return {255, 8, 5, 0};
        case PrinterState::Unknown:
        case PrinterState::Offline:   return {92, 38, 5, 0};
    }
    return {};
}

Rgbw interiorColor(WhiteMode mode) {
    switch (mode) {
        case WhiteMode::Warm: return {80, 31, 0, 175};
        case WhiteMode::Cool: return {0, 30, 80, 175};
        default:             return {0, 0, 0, 255};
    }
}

Effect automaticEffect(PrinterState state) {
    switch (state) {
        case PrinterState::Standby:   return Effect::Breathe;
        case PrinterState::Printing:  return Effect::Wave;
        case PrinterState::Paused:    return Effect::Breathe;
        case PrinterState::Complete:  return Effect::SuccessSweep;
        case PrinterState::Cancelled: return Effect::Pulse;
        case PrinterState::Error:     return Effect::ErrorAlert;
        default:                     return Effect::Breathe;
    }
}

float pixelLevel(Effect effect, float phase, float position, float progress, uint8_t length) {
    switch (effect) {
        case Effect::Breathe:
            return wave(phase);
        case Effect::Wave: {
            const float crest = wave(phase - position);
            return crest * crest;
        }
        case Effect::Comet: {
            const float tail = cycle(phase - position);
            const float peak = clamp01(1.0f - tail / 0.4f);
            return peak * peak;
        }
        case Effect::Scanner: {
            const float scanner = 1.0f - fabsf(2.0f * phase - 1.0f);
            return clamp01(1.0f - 5.0f * fabsf(position - scanner));
        }
        case Effect::Progress: {
            if (progress < 0.0f) return 0.05f;
            const float start = position * (length - 1);
            return 0.02f + 0.98f * clamp01(progress * length - start);
        }
        case Effect::Pulse: {
            const float peak = wave(phase);
            return peak * peak * peak;
        }
        case Effect::SuccessSweep: {
            const float sweep = phase * 1.5f - 0.25f;
            return clamp01(1.0f - 4.0f * fabsf(position - sweep));
        }
        case Effect::ErrorAlert: {
            const float first = clamp01(1.0f - 12.0f * fabsf(phase - 0.18f));
            const float second = clamp01(1.0f - 12.0f * fabsf(phase - 0.39f));
            return fmaxf(first, second);
        }
        case Effect::Chase: {
            const float band = cycle(position * (length - 1) / 4.5f - 2.0f * phase);
            return clamp01(band * 9.0f) * clamp01((0.48f - band) * 9.0f);
        }
        case Effect::CenterBurst: {
            const float distance = fabsf(2.0f * position - 1.0f);
            const float ring = phase * 1.25f;
            return clamp01(1.0f - 4.0f * fabsf(distance - ring));
        }
        case Effect::Sparkle: {
            const float step = 4.0f * phase;
            const uint32_t pixel = static_cast<uint32_t>(position * (length - 1) + 0.5f);
            const uint32_t slot = static_cast<uint32_t>(step);
            // Keep each pixel's selection stable throughout the fade.
            uint32_t seed = (pixel + 1) * 0x9e3779b9u ^ (slot + 1) * 0x85ebca6bu;
            seed = (seed ^ (seed >> 16)) * 0x7feb352du;
            const float life = wave(step - slot);
            return (seed >> 29) < 3 ? life * life : 0.0f;
        }
        case Effect::Wipe: {
            const float edge = 1.0f - fabsf(2.0f * phase - 1.0f);
            return clamp01(edge * (length + 1) - position * (length - 1));
        }
        default:  // Static in B and C: 0.8 on purpose (v0.9 look at low brightness).
            return 0.8f;
    }
}

}  // namespace

bool filamentColor(const LedSettings& config, const PrinterStatus& printer, uint32_t& rgb) {
    if (printer.head < 0 || printer.head >= HeadCount) return false;
    rgb = printer.hasFilamentColor ? printer.filamentRgb : config.fallback[printer.head];
    return true;
}

void Leds::begin(const LedSettings& config) {
    setLayout(config.boundaries);
    animations_[Interior].from = animations_[Interior].to = interiorColor(config.white);
    strip_.begin();
    strip_.clear();
    strip_.show();
    lastFrame_ = millis();
}

void Leds::setLayout(const LedBoundaries& boundaries) {
    boundaries_ = boundaries;
    showingLayout_ = false;
}

void Leds::previewLayout(const LedBoundaries& boundaries, uint32_t now) {
    boundaries_ = boundaries;
    layoutStarted_ = now;
    showingLayout_ = true;
}

void Leds::tick(uint32_t now, const LedSettings& config, const PrinterStatus& printer) {
    const auto elapsed = now - lastFrame_;
    if (elapsed < FrameInterval) return;
    lastFrame_ = now;
    if (showingLayout_ && now - layoutStarted_ >= LayoutPreviewMs) showingLayout_ = false;

    const bool stateChanged = state_ != printer.state;
    state_ = printer.state;
    uint32_t filament = 0;
    const bool useFilament = (state_ == PrinterState::Standby || state_ == PrinterState::Printing) &&
                             filamentColor(config, printer, filament);
    const auto displayFilament = useFilament && printer.hasFilamentColor
                                 ? mapFilamentColor(filament) : FilamentDisplayColor{filament, false};
    const Rgbw automaticColor = useFilament ? unpackRgb(displayFilament.rgb) : stateColor(state_);
    rainbowElapsed_ = (rainbowElapsed_ + elapsed % RainbowPeriodMs) % RainbowPeriodMs;
    const uint32_t rainbowPhase = rainbowElapsed_ * HueSteps / RainbowPeriodMs;

    bool changed = false;
    const auto draw = [this, &changed](uint8_t pixel, uint32_t color) {
        if (frame_[pixel] == color) return;
        frame_[pixel] = color;
        strip_.setPixelColor(pixel, color);
        changed = true;
    };

    for (uint8_t i = 0; i < ZoneCount; ++i) {
        const auto zone = static_cast<Zone>(i);
        const uint8_t start = i ? boundaries_[i - 1] : 0;
        const uint8_t end = i < boundaries_.size() ? boundaries_[i] : PixelCount;
        const uint8_t length = end - start;
        const auto drawSolid = [&draw, start, end](uint32_t color) {
            for (uint8_t pixel = start; pixel < end; ++pixel) draw(pixel, color);
        };
        const auto& settings = config.rgbZones[i];
        auto& animation = animations_[zone];
        Rgbw target = unpackRgb(settings.manualRgb);
        if (settings.automaticColor) target = automaticColor;
        else if (zone == Interior && config.interiorWhite) target = interiorColor(config.white);
        if (pack(target) != pack(animation.to)) {
            animation.from = blend(animation.from, animation.to, now - animation.blendStart);
            animation.to = target;
            animation.blendStart = now;
        }
        const Rgbw visible = blend(animation.from, animation.to, now - animation.blendStart);
        if (stateChanged) animation.phase = 0.0f;
        const bool automaticCenter = zone == Center && settings.effect == Effect::Auto;
        const float step = automaticCenter ? elapsed / 1800.0f
                                           : elapsed * (0.18f + 0.011f * settings.speed) / 1000.0f;
        animation.phase = cycle(animation.phase + step);
        float gain = zoneGain(config.zones[zone], config.limits[zone]);

        if (showingLayout_) {
            drawSolid(pack(unpackRgb(LayoutColors[zone]), min<uint8_t>(config.limits[zone], 12) / 100.0f));
            continue;
        }

        const bool rainbow = settings.automaticColor ? displayFilament.rainbow : config.manualRainbow[i];
        if (rainbow) {
            // Use the physical LED index so adjacent zones share the same gradient.
            for (uint8_t pixel = start; pixel < end; ++pixel) {
                const uint32_t offset = pixel * HueSteps / PixelCount;
                const auto hue = static_cast<uint16_t>(offset - rainbowPhase);
                // Two moving highlights make motion visible at low PWM values.
                const auto wavePhase = static_cast<uint16_t>(2 * offset - 3 * rainbowPhase);
                const float level = 0.25f + 0.75f * wave(wavePhase / static_cast<float>(HueSteps));
                const uint8_t value = scaled(255, gain * level);
                draw(pixel, Adafruit_NeoPixel::ColorHSV(hue, 255, value));
            }
            continue;
        }

        const bool solid = settings.effect == Effect::Static && (zone == Center || zone == Interior);
        if (automaticCenter || solid) {
            if (automaticCenter) {
                if (state_ == PrinterState::Error) gain *= wave(animation.phase);
                if (state_ == PrinterState::Offline || state_ == PrinterState::Unknown) gain *= 0.45f;
            }
            drawSolid(pack(visible, gain));
            continue;
        }
        const auto effect = settings.effect == Effect::Auto ? automaticEffect(state_) : settings.effect;
        for (uint8_t pixel = 0; pixel < length; ++pixel) {
            const uint8_t offset = settings.reverse ? length - 1 - pixel : pixel;
            const float position = length > 1 ? static_cast<float>(offset) / (length - 1) : 0.5f;
            const float level = pixelLevel(effect, animation.phase, position, printer.progress, length);
            draw(start + pixel, pack(visible, gain * level));
        }
    }
    if (changed) strip_.show();
}

uint32_t Leds::previewRgb(uint8_t pixel) const {
    const uint32_t color = frame_[pixel];
    const unsigned white = color >> 24;
    const uint32_t red = min<uint32_t>(255, ((color >> 16) & 255u) + white);
    const uint32_t green = min<uint32_t>(255, ((color >> 8) & 255u) + white);
    const uint32_t blue = min<uint32_t>(255, (color & 255u) + white);
    return (red << 16) | (green << 8) | blue;
}

}  // namespace u1
