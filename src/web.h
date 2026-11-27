#pragma once

#include "leds.h"
#include "network.h"
#include <ArduinoJson.h>
#include <WebServer.h>

namespace u1 {

class Panel {
public:
    Panel(SettingsStore& settings, Network& network, Leds& leds)
        : settings_(settings), network_(network), leds_(leds) {}
    void begin();
    void tick(const PrinterStatus& printer);

private:
    class HttpServer : public WebServer {
    public:
        using WebServer::WebServer;
        void handleClient() override;
        // The HTTP parser uses Stream's timeout in milliseconds.
        void setReadTimeoutMs(uint32_t timeout) {
            static_cast<Stream&>(_currentClient).setTimeout(timeout);
        }

    private:
        uint32_t lastIdleLog_ = 0;
    };

    bool readPost(JsonDocument& doc);
    void sendJson(int code, const JsonDocument& doc);
    void noStore();
    void error(int code, const char* message);
    void status();
    void settings();
    void saveLeds();
    void setLedLayout();
    void saveNetwork();
    void saveLanguage();
    bool firmwareAvailable();
    void receiveFirmware();
    void finishFirmware();
    static size_t firmwareCapacity();

    HttpServer server_{80};
    SettingsStore& settings_;
    Network& network_;
    Leds& leds_;
    PrinterStatus printer_;
    uint32_t firmwareRestartAt_ = 0;
    int firmwareStatus_ = 400;
    const char* firmwareMessage_ = "Wybierz plik firmware.bin.";
};

}  // namespace u1
