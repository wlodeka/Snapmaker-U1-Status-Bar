#include "network.h"

namespace u1 {
namespace {
constexpr uint32_t ConnectTimeout = 25000;
constexpr uint32_t RetryInterval = 20000;
constexpr uint32_t RecoveryDelay = 60000;
constexpr uint32_t SetupGrace = 10000;
constexpr uint32_t ApRetryInterval = 12000;
}

void Network::begin() {
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    const uint32_t now = millis();
    stationChangedAt_ = now;
    const auto deviceId = static_cast<unsigned>(ESP.getEfuseMac() >> 24) & 0xFFFFFF;
    snprintf(setupSsid_, sizeof(setupSsid_), "U1-Status-%06X", deviceId);

    if (settings_.network().ssid[0]) {
        lastAttempt_ = now;
        if (WiFi.mode(WIFI_STA)) connect(settings_.network());
        else Serial.println("Wi-Fi: nie można uruchomić trybu WIFI_STA.");
    } else {
        startSetup(now, false);
    }
}

void Network::startSetup(uint32_t now, bool recovery) {
    lastApChangeAttempt_ = now;
    const auto previousMode = WiFi.getMode();
    // Recovery keeps STA available for retries while the setup AP is open.
    const auto mode = recovery ? WIFI_AP_STA : WIFI_AP;
    if (!WiFi.mode(mode)) {
        Serial.printf("Wi-Fi: nie można włączyć trybu %s dla AP %s. Kolejna próba za 12 s.\n",
                      recovery ? "WIFI_AP_STA" : "WIFI_AP", setupSsid_);
        return;
    }
    setupAp_ = WiFi.softAP(setupSsid_);
    if (!setupAp_) {
        Serial.printf("Wi-Fi: uruchomienie AP %s nie powiodło się. Kolejna próba za 12 s.\n",
                      setupSsid_);
        WiFi.mode(previousMode);
        return;
    }
    Serial.printf("Wi-Fi: otwarty AP %s | panel http://%s/\n",
                  setupSsid_, WiFi.softAPIP().toString().c_str());
}

void Network::connect(const NetworkSettings& value) {
    WiFi.setMinSecurity(value.wifiPassword[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN);
    WiFi.begin(value.ssid, value.wifiPassword[0] ? value.wifiPassword : nullptr);
}

IPAddress Network::stationIp() const {
    return WiFi.status() == WL_CONNECTED ? WiFi.localIP() : IPAddress();
}

bool Network::isSetupInterface(IPAddress localIp) const {
    return setupAp_ && localIp == WiFi.softAPIP();
}

NetworkUpdate Network::apply(const NetworkSettings& value, uint32_t now) {
    if (phase_ != Phase::Idle) return NetworkUpdate::Pending;
    const auto& current = settings_.network();
    if (strcmp(current.ssid, value.ssid) == 0 &&
        strcmp(current.wifiPassword, value.wifiPassword) == 0) {
        update_ = settings_.save(value) ? NetworkUpdate::Saved : NetworkUpdate::StorageError;
    } else {
        candidate_ = value;
        phase_ = Phase::Scheduled;
        started_ = now;
        update_ = NetworkUpdate::Pending;
    }
    return update_;
}

void Network::finish(NetworkUpdate result) {
    update_ = result;
    phase_ = Phase::Idle;
    started_ = millis();
    candidate_ = {};
    if (result == NetworkUpdate::Saved) {
        Serial.println("Wi-Fi: zapisano nową konfigurację sieci.");
        return;
    }
    Serial.println(result == NetworkUpdate::Rejected
                       ? "Wi-Fi: brak adresu dla nowej sieci. Zachowano ustawienia."
                       : "Wi-Fi: błąd zapisu nowej konfiguracji w NVS.");

    WiFi.disconnect();
    if (setupAp_ && !settings_.network().ssid[0]) {
        WiFi.mode(WIFI_AP);
    }
    // A configured device retries the last saved network after a failed change.
    lastAttempt_ = started_ - RetryInterval + 500;
}

void Network::tick(uint32_t now) {
    const IPAddress ip = stationIp();
    if (ip != printedIp_) {
        printedIp_ = ip;
        stationChangedAt_ = now;
        if (ip != IPAddress()) Serial.printf("Panel: http://%s/\n", ip.toString().c_str());
    }

    if (phase_ != Phase::Idle) {
        if (now - started_ >= ConnectTimeout) {
            finish(NetworkUpdate::Rejected);
        } else if (phase_ == Phase::Scheduled && now - started_ >= 750) {
            if (WiFi.getMode() & WIFI_MODE_STA) WiFi.disconnect();
            phase_ = Phase::Disconnecting;
        } else if (phase_ == Phase::Disconnecting && ip == IPAddress()) {
            connect(candidate_);
            phase_ = Phase::Connecting;
        } else if (phase_ == Phase::Connecting && ip != IPAddress() &&
                   WiFi.SSID() == candidate_.ssid) {
            finish(settings_.save(candidate_) ? NetworkUpdate::Saved :
                                                NetworkUpdate::StorageError);
        }
        return;
    }

    const bool configured = settings_.network().ssid[0] != '\0';
    const bool online = ip != IPAddress();
    const bool needsRecovery = configured && !online && now - stationChangedAt_ >= RecoveryDelay;
    if (!setupAp_ && (!configured || needsRecovery) &&
        now - lastApChangeAttempt_ >= ApRetryInterval) {
        startSetup(now, configured);
    } else if (setupAp_ && configured && online && now - stationChangedAt_ >= SetupGrace &&
               now - lastApChangeAttempt_ >= 1000) {
        lastApChangeAttempt_ = now;
        if (WiFi.mode(WIFI_STA)) setupAp_ = false;
        else Serial.println("Wi-Fi: nie można wyłączyć AP i wrócić do trybu WIFI_STA.");
    }

    if (configured && !online && now - lastAttempt_ >= RetryInterval) {
        lastAttempt_ = now;
        connect(settings_.network());
    }
}

}  // namespace u1
