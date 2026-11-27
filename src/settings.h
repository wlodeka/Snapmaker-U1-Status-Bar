#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <array>

namespace u1 {

constexpr char FirmwareVersion[] = "1.0.0";
constexpr uint8_t HeadCount = 4;
constexpr uint8_t LogicalToolCount = 32;
constexpr uint8_t PixelCount = 60;
// Physical and stored order: B (Right), D (Center), C (Left), A (Interior).
enum Zone : uint8_t { Right, Center, Left, Interior, ZoneCount };
using LedBoundaries = std::array<uint8_t, ZoneCount - 1>;

enum class Effect : uint8_t {
    Auto, Static, Breathe, Wave, Comet, Scanner, Progress, Pulse,
    SuccessSweep, ErrorAlert, Chase, CenterBurst, Sparkle, Wipe, Count
};

enum class WhiteMode : uint8_t { Neutral, Warm, Cool, Count };
enum class PanelLanguage : uint8_t { Polish, English };

struct ZoneSettings {
    uint8_t enabled;
    uint8_t brightness;
};

struct RgbZoneSettings {
    Effect effect;
    uint8_t speed;
    uint8_t reverse;
    uint8_t automaticColor;
    uint32_t manualRgb;
};

struct LedSettings {
    ZoneSettings zones[ZoneCount];
    RgbZoneSettings rgbZones[ZoneCount];
    uint8_t limits[ZoneCount];
    uint8_t manualRainbow[ZoneCount];
    LedBoundaries boundaries;
    WhiteMode white;
    uint8_t interiorWhite;
    uint32_t fallback[HeadCount];
};

struct NetworkSettings {
    char ssid[33];
    char wifiPassword[65];
    char moonrakerIp[16];  // An empty address disables the connection.
    uint16_t moonrakerPort;
};

class SettingsStore {
public:
    void begin();
    bool save(const LedSettings& value);
    bool save(const NetworkSettings& value);
    bool save(PanelLanguage value);
    const LedSettings& led() const { return led_; }
    const NetworkSettings& network() const { return network_; }
    PanelLanguage language() const { return language_; }

private:
    Preferences nvs_;
    bool open_ = false;
    PanelLanguage language_ = PanelLanguage::Polish;
    LedSettings led_ = {
        {{1, 45}, {1, 75}, {1, 45}, {0, 65}},
        {{Effect::Auto, 45, 0, 1, 0x1BA6FF},
         {Effect::Auto, 45, 0, 1, 0x1BA6FF},
         {Effect::Auto, 45, 1, 1, 0x1BA6FF},
         {Effect::Static, 45, 0, 0, 0xF4EBDF}},
        {55, 55, 55, 95}, {}, {{9, 33, 42}},
        WhiteMode::Neutral, 1,
        {0x1BA6FF, 0xFF5538, 0x42E086, 0xF4EBDF}
    };
    NetworkSettings network_ = {"", "", "", 7125};
};

bool parseHex(const char* text, size_t digits, uint32_t& value);
bool isMoonrakerIp(const char* text);
bool validNetwork(const NetworkSettings& config);
bool validLedBoundaries(const LedBoundaries& boundaries);

}  // namespace u1
