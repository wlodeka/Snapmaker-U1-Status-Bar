#pragma once

#include "settings.h"
#include <ArduinoJson.h>

namespace u1 {

inline int parseTool(JsonString value) {
    constexpr char Prefix[] = "extruder";
    constexpr size_t PrefixLength = sizeof(Prefix) - 1;
    if (!value.c_str() || value.size() < PrefixLength ||
        memcmp(value.c_str(), Prefix, PrefixLength) != 0) return -1;
    const size_t digits = value.size() - PrefixLength;
    const char* suffix = value.c_str() + PrefixLength;
    if (!digits) return 0;
    if (digits > 2 || suffix[0] < '0' || suffix[0] > '9') return -1;
    int tool = suffix[0] - '0';
    if (digits == 2) {
        if (tool == 0 || suffix[1] < '0' || suffix[1] > '9') return -1;
        tool = tool * 10 + suffix[1] - '0';
    }
    return tool < LogicalToolCount ? tool : -1;
}

struct TaskData {
    uint32_t colors[HeadCount]{};
    uint8_t colorMask = 0;
    uint8_t usedMask = 0;
    int8_t map[LogicalToolCount]{};
    bool hasMap = false;

    void update(JsonObjectConst data, bool active) {
        if (data.containsKey("filament_color_rgba")) {
            colorMask = 0;
            const auto values = data["filament_color_rgba"].as<JsonArrayConst>();
            if (values.size() == HeadCount) {
                for (uint8_t head = 0; head < HeadCount; ++head) {
                    const auto value = values[head].as<JsonString>();
                    uint32_t rgba;
                    if (value.size() == 8 && parseHex(value.c_str(), 8, rgba)) {
                        colors[head] = rgba >> 8;  // U1 publishes RRGGBBAA. Alpha is not white.
                        colorMask |= 1u << head;
                    }
                }
            }
        }
        if (!active) return;
        if (data.containsKey("extruder_map_table")) {
            const auto values = data["extruder_map_table"].as<JsonArrayConst>();
            hasMap = values.size() == LogicalToolCount;
            if (hasMap) {
                for (uint8_t tool = 0; tool < LogicalToolCount; ++tool) {
                    const auto value = values[tool];
                    const int head = value | -1;
                    map[tool] = value.is<int>() && head >= 0 && head < HeadCount ? head : -1;
                }
            }
        }
        if (data.containsKey("extruders_used")) {
            usedMask = 0;
            const auto values = data["extruders_used"].as<JsonArrayConst>();
            if (values.size() == HeadCount) {
                for (uint8_t head = 0; head < HeadCount; ++head) {
                    if (!values[head].is<bool>()) {
                        usedMask = 0;
                        break;
                    }
                    if (values[head].as<bool>()) usedMask |= 1u << head;
                }
            }
        }
    }

    int resolve(int tool, bool active) const {
        if (tool < 0 || tool >= LogicalToolCount) return -1;
        // The stock U1 resolves logical tools in Klipper before publishing toolhead.extruder.
        if (tool < HeadCount) return tool;
        if (!active || !hasMap || map[tool] < 0) return -1;
        const int head = map[tool];
        return (usedMask & (1u << head)) ? head : -1;
    }
};

}  // namespace u1
