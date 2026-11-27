#pragma once

#include "settings.h"
#include <WiFi.h>

namespace u1 {

enum class NetworkUpdate { Idle, Pending, Saved, Rejected, StorageError };

class Network {
public:
    explicit Network(SettingsStore& settings) : settings_(settings) {}
    void begin();
    void tick(uint32_t now);
    NetworkUpdate apply(const NetworkSettings& value, uint32_t now);
    NetworkUpdate update() const { return update_; }
    IPAddress stationIp() const;
    bool isSetupInterface(IPAddress localIp) const;

private:
    enum class Phase { Idle, Scheduled, Disconnecting, Connecting };
    void startSetup(uint32_t now, bool recovery);
    void connect(const NetworkSettings& value);
    void finish(NetworkUpdate result);

    SettingsStore& settings_;
    NetworkSettings candidate_{};
    char setupSsid_[24]{};
    bool setupAp_ = false;
    NetworkUpdate update_ = NetworkUpdate::Idle;
    Phase phase_ = Phase::Idle;
    uint32_t started_ = 0;
    uint32_t lastAttempt_ = 0;
    uint32_t stationChangedAt_ = 0;
    uint32_t lastApChangeAttempt_ = 0;
    IPAddress printedIp_;
};

}  // namespace u1
