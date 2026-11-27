#include "web.h"
#include "panel_page.h"
#include "requests.h"
#include <math.h>

namespace u1 {
namespace {

constexpr char ContentType[] = "application/json";
constexpr uint32_t IdleClientTimeout = 750;

const char* languageCode(PanelLanguage language) {
    return language == PanelLanguage::English ? "en" : "pl";
}

const char* stateLabel(PrinterState state, bool connected) {
    switch (state) {
        case PrinterState::Standby:   return "Gotowość";
        case PrinterState::Printing:  return "Drukowanie";
        case PrinterState::Paused:    return "Pauza";
        case PrinterState::Complete:  return "Wydruk zakończony";
        case PrinterState::Cancelled: return "Wydruk anulowany";
        case PrinterState::Error:     return "Błąd drukarki";
        case PrinterState::Unknown:   return "Nieznany stan";
        case PrinterState::Offline:   return connected ? "Brak danych z Klippera" : "Brak połączenia";
    }
    return "Nieznany stan";
}

const char* networkResult(NetworkUpdate update) {
    switch (update) {
        case NetworkUpdate::Pending: return "pending";
        case NetworkUpdate::Saved: return "saved";
        case NetworkUpdate::Rejected: return "rejected";
        case NetworkUpdate::StorageError: return "storageError";
        default: return "idle";
    }
}

void hexColor(char* buffer, uint32_t rgb) {
    snprintf(buffer, 8, "#%06lx", static_cast<unsigned long>(rgb));
}

}  // namespace

void Panel::HttpServer::handleClient() {
    if (_currentStatus == HC_NONE) {
        _currentClient = _server.available();
        if (!_currentClient) return;
        setReadTimeoutMs(500);
        _currentStatus = HC_WAIT_READ;
        _statusChange = millis();
    }
    const uint32_t now = millis();
    // Do not let an empty browser preconnection hold the only HTTP slot for five seconds.
    if (_currentStatus == HC_WAIT_READ && now - _statusChange >= IdleClientTimeout &&
        _currentClient.connected() && !_currentClient.available()) {
        _currentClient.stop();
        if (!lastIdleLog_ || now - lastIdleLog_ >= 10000) {
            Serial.println("Panel WWW: zamknięto bezczynne połączenie HTTP.");
            lastIdleLog_ = now;
        }
    }
    WebServer::handleClient();
}

void Panel::begin() {
    const char* headers[] = {"Content-Type"};
    server_.collectHeaders(headers, 1);
    server_.on("/", HTTP_GET, [this] {
        noStore();
        server_.sendHeader("X-Frame-Options", "DENY");
        server_.sendHeader("Content-Security-Policy",
                           "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
                           "connect-src 'self'; base-uri 'none'; form-action 'self'; frame-ancestors 'none'");
        server_.sendHeader("Content-Encoding", "gzip");
        server_.send_P(200, PSTR("text/html; charset=utf-8"),
                       reinterpret_cast<const char*>(PanelHtmlGzip), sizeof(PanelHtmlGzip));
    });
    server_.on("/api/status", HTTP_GET, [this] { status(); });
    server_.on("/api/settings", HTTP_GET, [this] { settings(); });
    server_.on("/api/led", HTTP_POST, [this] { saveLeds(); });
    server_.on("/api/led/layout", HTTP_POST, [this] { setLedLayout(); });
    server_.on("/api/network", HTTP_POST, [this] { saveNetwork(); });
    server_.on("/api/language", HTTP_POST, [this] { saveLanguage(); });
    server_.on("/api/firmware", HTTP_POST,
               [this] { finishFirmware(); }, [this] { receiveFirmware(); });
    server_.onNotFound([this] {
        error(404, "Strona jest niedostępna.");
    });
    server_.begin();
    Serial.println("Panel WWW: uruchomiono obsługę HTTP na porcie 80.");
}

void Panel::tick(const PrinterStatus& printer) {
    if (firmwareRestartAt_) {
        if (static_cast<int32_t>(millis() - firmwareRestartAt_) >= 0) ESP.restart();
        return;
    }
    printer_ = printer;
    server_.handleClient();
}

bool Panel::readPost(JsonDocument& doc) {
    String contentType = server_.header("Content-Type");
    const int semicolon = contentType.indexOf(';');
    if (semicolon >= 0) contentType.remove(semicolon);
    contentType.trim();
    if (!contentType.equalsIgnoreCase(ContentType)) {
        error(415, "Wyślij ustawienia w formacie JSON.");
        return false;
    }
    const String body = server_.arg("plain");
    if (body.length() > 2048) {
        error(413, "Dane ustawień są za duże.");
        return false;
    }
    if (!body.length() || body.length() != static_cast<size_t>(server_.clientContentLength())) {
        error(400, "Brak lub niepełna treść żądania.");
        return false;
    }
    const auto parsed = deserializeJson(doc, body.c_str(), body.length(),
                                        DeserializationOption::NestingLimit(6));
    if (parsed || !doc.is<JsonObject>()) {
        error(400, "Treść żądania musi być poprawnym obiektem JSON.");
        return false;
    }
    return true;
}

void Panel::sendJson(int code, const JsonDocument& doc) {
    String response;
    const auto length = measureJson(doc);
    const bool serialized = !doc.overflowed() && response.reserve(length) &&
                            serializeJson(doc, response) == length;
    const String path = server_.uri();
    const bool frequent = path == "/api/status" || path == "/api/led/layout";
    if (code != 404 && (!frequent || !serialized || code >= 400)) {
        const char* detail = serialized ? (doc["error"] | "") : "Błąd tworzenia odpowiedzi JSON.";
        Serial.printf("Panel WWW: %s %s -> %d%s%s\n",
                      server_.method() == HTTP_POST ? "POST" : "GET", path.c_str(),
                      serialized ? code : 500, *detail ? " | " : "", detail);
    }
    noStore();
    if (serialized) server_.send(code, ContentType, response);
    else server_.send(500, ContentType, R"({"error":"Błąd tworzenia odpowiedzi JSON."})");
}

void Panel::noStore() {
    server_.sendHeader("Cache-Control", "no-store");
    server_.sendHeader("X-Content-Type-Options", "nosniff");
}

void Panel::error(int code, const char* message) {
    StaticJsonDocument<128> data;
    data["error"] = message;
    sendJson(code, data);
}

void Panel::status() {
    StaticJsonDocument<1024> data;
    const IPAddress ip = network_.stationIp();
    const bool online = ip != IPAddress();
    data["ip"] = online ? ip.toString() : String();
    data["setup"] = network_.isSetupInterface(server_.client().localIP());
    data["networkUpdate"] = networkResult(network_.update());
    data["firmwareVersion"] = FirmwareVersion;
    data["firmwareCapacity"] = firmwareCapacity();
    data["firmwareAvailable"] = firmwareAvailable();
    data["moonrakerConfigured"] = settings_.network().moonrakerIp[0] != '\0';
    data["moonrakerConnected"] = printer_.connected;
    data["printer"] = stateLabel(printer_.state, printer_.connected);
    if (printer_.head >= 0) data["head"] = printer_.head;
    else data["head"] = nullptr;
    if (printer_.progress >= 0) {
        data["progress"] = static_cast<unsigned>(lroundf(printer_.progress * 100.0f));
    } else {
        data["progress"] = nullptr;
    }

    uint32_t rgb;
    char color[8];
    if (filamentColor(settings_.led(), printer_, rgb)) {
        hexColor(color, rgb);
        data["filament"] = color;
        data["fallback"] = !printer_.hasFilamentColor;
    } else {
        data["filament"] = nullptr;
    }

    char pixels[PixelCount * 6 + 1];
    for (uint8_t i = 0; i < PixelCount; ++i) {
        snprintf(pixels + 6 * i, 7, "%06lx", static_cast<unsigned long>(leds_.previewRgb(i)));
    }
    data["pixels"] = pixels;
    sendJson(200, data);
}

void Panel::settings() {
    StaticJsonDocument<1536> data;
    const auto& config = settings_.led();
    data["language"] = languageCode(settings_.language());
    auto led = data.createNestedObject("led");
    auto boundaries = led.createNestedArray("boundaries");
    for (const auto boundary : leds_.boundaries()) boundaries.add(boundary);
    auto zones = led.createNestedArray("zones");
    for (uint8_t i = 0; i < ZoneCount; ++i) {
        auto entry = zones.createNestedObject();
        const auto& zone = config.zones[i];
        entry["enabled"] = static_cast<bool>(zone.enabled);
        entry["brightness"] = zone.brightness;
        entry["limit"] = config.limits[i];
        const auto& rgb = config.rgbZones[i];
        char color[8];
        hexColor(color, rgb.manualRgb);
        entry["effect"] = static_cast<uint8_t>(rgb.effect);
        entry["speed"] = rgb.speed;
        entry["reverse"] = static_cast<bool>(rgb.reverse);
        entry["automaticColor"] = static_cast<bool>(rgb.automaticColor);
        entry["manualRainbow"] = static_cast<bool>(config.manualRainbow[i]);
        entry["manualColor"] = JsonString(color, JsonString::Copied);
    }
    led["white"] = static_cast<uint8_t>(config.white);
    led["interiorWhite"] = static_cast<bool>(config.interiorWhite);
    auto fallback = led.createNestedArray("fallback");
    for (const auto rgb : config.fallback) {
        char color[8];
        hexColor(color, rgb);
        fallback.add(JsonString(color, JsonString::Copied));
    }

    const auto& current = settings_.network();
    auto network = data.createNestedObject("network");
    network["ssid"] = current.ssid;
    network["moonrakerIp"] = current.moonrakerIp;
    network["moonrakerPort"] = current.moonrakerPort;
    network["openWifi"] = current.ssid[0] && !current.wifiPassword[0];
    sendJson(200, data);
}

void Panel::saveLeds() {
    StaticJsonDocument<1536> data;
    if (!readPost(data)) return;
    LedSettings value{};
    if (!readLeds(data.as<JsonObjectConst>(), value)) {
        error(400, "Sprawdź wartości ustawień LED.");
    } else if (!settings_.save(value)) {
        error(500, "Błąd zapisu ustawień LED w pamięci.");
    } else {
        leds_.setLayout(value.boundaries);
        data.to<JsonObject>();
        sendJson(200, data);
    }
}

void Panel::setLedLayout() {
    StaticJsonDocument<256> data;
    if (!readPost(data)) return;
    LedBoundaries boundaries{};
    if (!readLedBoundaries(data["boundaries"].as<JsonArrayConst>(), boundaries)) {
        error(400, "Podaj trzy rosnące granice stref z zakresu 1–59.");
        return;
    }
    leds_.previewLayout(boundaries, millis());
    data.to<JsonObject>();
    sendJson(200, data);
}

void Panel::saveNetwork() {
    StaticJsonDocument<1024> data;
    if (!readPost(data)) return;
    if (network_.update() == NetworkUpdate::Pending) {
        error(409, "Trwa sprawdzanie połączenia Wi-Fi.");
        return;
    }
    NetworkSettings value{};
    if (!readNetwork(data.as<JsonObjectConst>(), settings_.network(), value)) {
        error(400, "Sprawdź SSID, hasło Wi-Fi, adres IPv4 i port.");
        return;
    }
    const auto result = network_.apply(value, millis());
    if (result == NetworkUpdate::StorageError) {
        error(500, "Błąd zapisu ustawień sieci w pamięci.");
        return;
    }
    data.clear();
    data["result"] = networkResult(result);
    sendJson(result == NetworkUpdate::Pending ? 202 : 200, data);
}

void Panel::saveLanguage() {
    StaticJsonDocument<128> data;
    if (!readPost(data)) return;
    char code[3];
    if (!readString(data["language"], code) || (strcmp(code, "pl") != 0 && strcmp(code, "en") != 0)) {
        error(400, "Wybierz język Polski lub English.");
        return;
    }
    const auto language = strcmp(code, "en") == 0 ? PanelLanguage::English : PanelLanguage::Polish;
    if (!settings_.save(language)) {
        error(500, "Błąd zapisu języka w pamięci.");
        return;
    }
    data.clear();
    data["language"] = languageCode(settings_.language());
    sendJson(200, data);
}

}  // namespace u1
