// In-memory NVS namespace so migration code runs on the host.
#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

class Preferences {
public:
    static inline std::map<std::string, std::vector<uint8_t>> store;

    bool begin(const char*, bool) { return true; }
    bool isKey(const char* key) { return store.count(key) != 0; }
    bool remove(const char* key) { return store.erase(key) != 0; }
    size_t getBytesLength(const char* key) { return isKey(key) ? store[key].size() : 0; }
    size_t getBytes(const char* key, void* buffer, size_t length) {
        if (!isKey(key) || store[key].size() > length) return 0;
        std::memcpy(buffer, store[key].data(), store[key].size());
        return store[key].size();
    }
    size_t putBytes(const char* key, const void* value, size_t length) {
        const auto* bytes = static_cast<const uint8_t*>(value);
        store[key].assign(bytes, bytes + length);
        return length;
    }
    uint8_t getUChar(const char* key) { return isKey(key) ? store[key][0] : 0; }
    size_t putUChar(const char* key, uint8_t value) { return putBytes(key, &value, 1); }
};
