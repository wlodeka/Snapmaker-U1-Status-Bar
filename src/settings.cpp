#include "settings.h"

namespace u1 {
namespace {

// Versioned keys allow future layouts to be introduced without reinterpreting old blobs.
constexpr char LedKey[] = "led_v3";
constexpr char LegacyLedV2Key[] = "led_v2";
constexpr char LegacyLedV1Key[] = "led_v1";
constexpr char NetworkKey[] = "net_v2";
constexpr char LegacyNetworkKey[] = "net_v1";
constexpr char LanguageKey[] = "panel_lang";

struct LegacyLedV2 {
    ZoneSettings zones[ZoneCount];
    RgbZoneSettings rgbZones[Interior];
    uint8_t brightnessLimit;
    WhiteMode white;
    uint32_t fallback[HeadCount];
};

struct LegacyLedV1 {
    ZoneSettings zones[ZoneCount];
    struct { Effect effect; uint8_t speed; uint8_t reverse; } sides[2];
    uint8_t brightnessLimit;
    uint8_t automaticColor;
    WhiteMode white;
    uint32_t fallback[HeadCount];
};

static_assert(sizeof(LegacyLedV2) == 52, "Unexpected led_v2 layout.");
static_assert(sizeof(LegacyLedV1) == 36, "Unexpected led_v1 layout.");

struct LegacyNetworkSettings {
    char ssid[33];
    char wifiPassword[65];
    uint8_t retired[65];  // Preserve the offsets in v0.1's stored layout.
    char moonrakerIp[16];
    uint16_t moonrakerPort;
};

template <typename T>
bool readSettings(Preferences& nvs, const char* key, T& value) {
    return nvs.getBytesLength(key) == sizeof(value) &&
           nvs.getBytes(key, &value, sizeof(value)) == sizeof(value);
}

bool valid(const LedSettings& config) {
    for (uint8_t i = 0; i < ZoneCount; ++i) {
        const auto& zone = config.zones[i];
        const auto& rgb = config.rgbZones[i];
        if (zone.enabled > 1 || zone.brightness > 100 || config.limits[i] > 100 ||
            config.manualRainbow[i] > 1 ||
            rgb.effect >= Effect::Count || rgb.speed < 1 || rgb.speed > 100 ||
            rgb.reverse > 1 || rgb.automaticColor > 1 || rgb.manualRgb > 0xFFFFFF) return false;
    }
    for (const auto color : config.fallback) {
        if (color > 0xFFFFFF) return false;
    }
    return config.white < WhiteMode::Count && config.interiorWhite <= 1 &&
           validLedBoundaries(config.boundaries);
}

template <typename Legacy>
void importLedSettings(const Legacy& old, LedSettings& led) {
    memcpy(led.zones, old.zones, sizeof(led.zones));
    memcpy(led.fallback, old.fallback, sizeof(led.fallback));
    led.white = old.white;
    for (uint8_t i = 0; i < Interior; ++i) led.limits[i] = old.brightnessLimit;
}

}  // namespace

bool validLedBoundaries(const LedBoundaries& boundaries) {
    uint8_t previous = 0;
    for (const uint8_t boundary : boundaries) {
        if (boundary <= previous || boundary >= PixelCount) return false;
        previous = boundary;
    }
    return true;
}

bool validNetwork(const NetworkSettings& config) {
    const auto ssidLength = strnlen(config.ssid, sizeof(config.ssid));
    const auto wifiLength = strnlen(config.wifiPassword, sizeof(config.wifiPassword));
    const auto ipLength = strnlen(config.moonrakerIp, sizeof(config.moonrakerIp));
    if (!ssidLength || ssidLength >= sizeof(config.ssid) ||
        ipLength >= sizeof(config.moonrakerIp) || !config.moonrakerPort) return false;
    if (wifiLength && (wifiLength < 8 || wifiLength >= sizeof(config.wifiPassword))) {
        return false;
    }
    uint32_t part;
    if (wifiLength == 64) {
        for (size_t offset = 0; offset < wifiLength; offset += 8) {
            if (!parseHex(config.wifiPassword + offset, 8, part)) return false;
        }
    }
    return !ipLength || isMoonrakerIp(config.moonrakerIp);
}

void SettingsStore::begin() {
    open_ = nvs_.begin("u1status", false);
    if (!open_) {
        Serial.println("NVS: nie można otworzyć konfiguracji.");
        return;
    }
    if (nvs_.isKey(LanguageKey) &&
        nvs_.getUChar(LanguageKey) == static_cast<uint8_t>(PanelLanguage::English)) {
        language_ = PanelLanguage::English;
    }
    LedSettings led{};
    if (nvs_.isKey(LedKey) && readSettings(nvs_, LedKey, led) && valid(led)) {
        led_ = led;
    } else {
        bool restored = false;
        LegacyLedV2 v2{};
        if (nvs_.isKey(LegacyLedV2Key) && readSettings(nvs_, LegacyLedV2Key, v2)) {
            led = led_;
            importLedSettings(v2, led);
            memcpy(led.rgbZones, v2.rgbZones, sizeof(v2.rgbZones));
            restored = valid(led);
        }
        LegacyLedV1 v1{};
        if (!restored && nvs_.isKey(LegacyLedV1Key) && readSettings(nvs_, LegacyLedV1Key, v1)) {
            led = led_;
            importLedSettings(v1, led);
            led.rgbZones[Right] = {v1.sides[0].effect, v1.sides[0].speed, v1.sides[0].reverse,
                                   1, led.rgbZones[Right].manualRgb};
            led.rgbZones[Left] = {v1.sides[1].effect, v1.sides[1].speed, v1.sides[1].reverse,
                                  1, led.rgbZones[Left].manualRgb};
            led.rgbZones[Center].automaticColor = v1.automaticColor;
            led.rgbZones[Center].manualRgb = v1.fallback[0];
            restored = valid(led);
        }
        // Restore legacy data without writing to NVS on boot.
        if (restored) led_ = led;
    }
    const bool hasLegacy = nvs_.isKey(LegacyNetworkKey);
    NetworkSettings network{};
    if (nvs_.isKey(NetworkKey) && readSettings(nvs_, NetworkKey, network) && validNetwork(network)) {
        network_ = network;
    } else if (hasLegacy) {
        LegacyNetworkSettings old{};
        if (!readSettings(nvs_, LegacyNetworkKey, old)) {
            Serial.println("NVS: nie można odczytać starego zapisu sieci.");
            return;
        }
        memcpy(network.ssid, old.ssid, sizeof(network.ssid));
        memcpy(network.wifiPassword, old.wifiPassword, sizeof(network.wifiPassword));
        memcpy(network.moonrakerIp, old.moonrakerIp, sizeof(network.moonrakerIp));
        network.moonrakerPort = old.moonrakerPort;
        if (!validNetwork(network)) {
            Serial.println("NVS: stary zapis sieci jest nieprawidłowy.");
            return;
        }
        network_ = network;
        if (!save(network)) {
            Serial.println("NVS: migracja sieci nie powiodła się. Zachowano stary zapis.");
            return;
        }
    }
    if (hasLegacy && !nvs_.remove(LegacyNetworkKey)) {
        Serial.println("NVS: nie można usunąć starego zapisu sieci.");
    }
}

bool SettingsStore::save(const LedSettings& value) {
    if (!open_ || !valid(value) || nvs_.putBytes(LedKey, &value, sizeof(value)) !=
                                        sizeof(value)) return false;
    led_ = value;
    return true;
}

bool SettingsStore::save(const NetworkSettings& value) {
    if (!open_ || !validNetwork(value) || nvs_.putBytes(NetworkKey, &value, sizeof(value)) !=
                                        sizeof(value)) return false;
    network_ = value;
    return true;
}

bool SettingsStore::save(PanelLanguage value) {
    if (!open_ || value > PanelLanguage::English) return false;
    if (value == language_) return true;
    if (nvs_.putUChar(LanguageKey, static_cast<uint8_t>(value)) != sizeof(uint8_t)) return false;
    language_ = value;
    return true;
}

// Callers that need an exact string length check it before parsing.
bool parseHex(const char* text, size_t digits, uint32_t& value) {
    value = 0;
    for (size_t i = 0; i < digits; ++i) {
        char ch = text[i];
        if (ch >= '0' && ch <= '9') ch -= '0';
        else if (ch >= 'a' && ch <= 'f') ch = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') ch = ch - 'A' + 10;
        else return false;
        value = (value << 4) | ch;
    }
    return true;
}

bool isMoonrakerIp(const char* text) {
    IPAddress ip;
    if (!ip.fromString(text) || ip.toString() != text) return false;
    return ip[0] != 0 && ip[0] != 127 && ip[0] < 224;
}

}  // namespace u1
