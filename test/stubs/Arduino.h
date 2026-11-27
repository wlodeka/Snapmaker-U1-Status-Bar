// Host-only stand-in for the few Arduino APIs used by settings.cpp.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

struct HostSerial {
    void println(const char*) {}
};
inline HostSerial Serial;

// shortcut: accepts only dotted-quad text like the ESP32 core; not a full port of it.
class IPAddress {
public:
    bool fromString(const char* text) {
        unsigned parts[4];
        char tail;
        if (std::sscanf(text, "%u.%u.%u.%u%c", &parts[0], &parts[1], &parts[2], &parts[3],
                        &tail) != 4) return false;
        for (int i = 0; i < 4; ++i) {
            if (parts[i] > 255) return false;
            bytes_[i] = static_cast<uint8_t>(parts[i]);
        }
        return true;
    }
    std::string toString() const {
        char text[16];
        std::snprintf(text, sizeof(text), "%u.%u.%u.%u", bytes_[0], bytes_[1], bytes_[2], bytes_[3]);
        return text;
    }
    uint8_t operator[](int index) const { return bytes_[index]; }

private:
    uint8_t bytes_[4]{};
};
