#pragma once

#include "settings.h"
#include <ArduinoJson.h>

namespace u1 {

inline bool integer(JsonVariantConst field, int lower, int upper, uint8_t& output) {
    if (!field.is<int>()) return false;
    const int value = field.as<int>();
    if (value < lower || value > upper) return false;
    output = static_cast<uint8_t>(value);
    return true;
}

inline bool flag(JsonVariantConst field, uint8_t& output) {
    if (!field.is<bool>()) return false;
    output = field.as<bool>();
    return true;
}

template <size_t N>
inline bool readString(JsonVariantConst field, char (&output)[N]) {
    if (!field.is<const char*>()) return false;
    const auto value = field.as<JsonString>();
    if (value.size() >= N || strlen(value.c_str()) != value.size()) return false;
    for (size_t i = 0; i < value.size(); ++i) {
        const auto byte = static_cast<unsigned char>(value.c_str()[i]);
        if (byte < 0x20 || byte == 0x7F) return false;
    }
    memcpy(output, value.c_str(), value.size() + 1);
    return true;
}

inline bool readColor(JsonVariantConst field, uint32_t& rgb) {
    const auto color = field.as<JsonString>();
    return color.size() == 7 && color.c_str()[0] == '#' &&
           parseHex(color.c_str() + 1, 6, rgb);
}

inline bool readLedBoundaries(JsonArrayConst input, LedBoundaries& boundaries) {
    if (input.size() != boundaries.size()) return false;
    for (size_t i = 0; i < boundaries.size(); ++i) {
        if (!integer(input[i], 1, PixelCount - 1, boundaries[i])) return false;
    }
    return validLedBoundaries(boundaries);
}

inline bool readLeds(JsonObjectConst body, LedSettings& config) {
    const auto zones = body["zones"].as<JsonArrayConst>();
    const auto fallback = body["fallback"].as<JsonArrayConst>();
    uint8_t white;
    if (zones.size() != ZoneCount || fallback.size() != HeadCount ||
        !readLedBoundaries(body["boundaries"].as<JsonArrayConst>(), config.boundaries) ||
        !flag(body["interiorWhite"], config.interiorWhite) ||
        !integer(body["white"], 0, static_cast<int>(WhiteMode::Count) - 1, white)) return false;
    config.white = static_cast<WhiteMode>(white);
    for (uint8_t i = 0; i < ZoneCount; ++i) {
        const auto fields = zones[i].as<JsonObjectConst>();
        auto& zone = config.rgbZones[i];
        uint8_t effect;
        if (!flag(fields["enabled"], config.zones[i].enabled) ||
            !integer(fields["brightness"], 0, 100, config.zones[i].brightness) ||
            !integer(fields["limit"], 0, 100, config.limits[i]) ||
            !integer(fields["effect"], 0, static_cast<int>(Effect::Count) - 1, effect) ||
            !integer(fields["speed"], 1, 100, zone.speed) ||
            !flag(fields["reverse"], zone.reverse) ||
            !flag(fields["automaticColor"], zone.automaticColor) ||
            !flag(fields["manualRainbow"], config.manualRainbow[i]) ||
            !readColor(fields["manualColor"], zone.manualRgb)) return false;
        zone.effect = static_cast<Effect>(effect);
    }
    for (uint8_t i = 0; i < HeadCount; ++i) {
        if (!readColor(fallback[i], config.fallback[i])) return false;
    }
    return true;
}

inline bool readNetwork(JsonObjectConst body, const NetworkSettings& current, NetworkSettings& next) {
    if (!readString(body["ssid"], next.ssid) ||
        !readString(body["wifiPassword"], next.wifiPassword) ||
        !readString(body["moonrakerIp"], next.moonrakerIp) ||
        !body["openWifi"].is<bool>() || !body["moonrakerPort"].is<uint16_t>()) return false;
    next.moonrakerPort = body["moonrakerPort"].as<uint16_t>();
    if (body["openWifi"].as<bool>()) {
        if (next.wifiPassword[0]) return false;
    } else if (!next.wifiPassword[0]) {
        if (strcmp(next.ssid, current.ssid) != 0) return false;
        strlcpy(next.wifiPassword, current.wifiPassword, sizeof(next.wifiPassword));
        if (!next.wifiPassword[0]) return false;
    }
    return validNetwork(next);
}

}  // namespace u1
