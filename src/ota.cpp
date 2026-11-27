#include "web.h"
#include <Update.h>
#include <esp_ota_ops.h>

namespace u1 {
namespace {
constexpr char FirmwareType[] = "application/octet-stream";
constexpr int Receiving = 100;
constexpr uint32_t FirmwareReadTimeoutMs = 2000;
}

size_t Panel::firmwareCapacity() {
    static const size_t capacity = []() -> size_t {
        const auto* next = esp_ota_get_next_update_partition(nullptr);
        const auto* metadata = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                        ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
        return next && metadata && esp_ota_get_app_partition_count() >= 2 ? next->size : 0;
    }();
    return capacity;
}

bool Panel::firmwareAvailable() {
    const auto ip = network_.stationIp();
    if (!firmwareCapacity() || ip == IPAddress() ||
        network_.update() == NetworkUpdate::Pending || WiFi.getMode() != WIFI_STA ||
        server_.client().localIP() != ip) return false;
    const auto host = server_.hostHeader();
    const auto address = ip.toString();
    return host == address || host == address + ":80";
}

void Panel::receiveFirmware() {
    // WebServer also calls this callback for multipart uploads.
    if (server_.header("Content-Type") != FirmwareType) return;
    auto& chunk = server_.raw();
    switch (chunk.status) {
        case RAW_START: {
            firmwareStatus_ = 400;
            firmwareMessage_ = "Brak pełnego obrazu firmware.";
            if (!firmwareAvailable()) {
                firmwareStatus_ = 403;
                firmwareMessage_ = "Aktualizacja jest dostępna przez adres IPv4 w domowej sieci i wymaga partycji OTA.";
                return;
            }
            const int length = server_.clientContentLength();
            if (length <= 0) return;
            if (static_cast<size_t>(length) > firmwareCapacity()) {
                firmwareStatus_ = 413;
                firmwareMessage_ = "Plik przekracza rozmiar partycji OTA.";
                return;
            }
            if (!Update.begin(length, U_FLASH)) {
                firmwareStatus_ = 500;
                firmwareMessage_ = "Nie można rozpocząć zapisu firmware.";
                return;
            }
            firmwareStatus_ = Receiving;
            server_.setReadTimeoutMs(FirmwareReadTimeoutMs);
            break;
        }
        case RAW_WRITE:
            if (firmwareStatus_ == Receiving &&
                Update.write(chunk.buf, chunk.currentSize) != chunk.currentSize) {
                Serial.printf("OTA: %s\n", Update.errorString());
                if (Update.isRunning()) Update.abort();
                firmwareStatus_ = 422;
                firmwareMessage_ = "Nie można zapisać obrazu firmware.";
            }
            break;
        case RAW_END:
            if (firmwareStatus_ == Receiving) {
                firmwareStatus_ = Update.isFinished() ? 200 : 400;
                if (firmwareStatus_ != 200) Update.abort();
            }
            break;
        case RAW_ABORTED:
            if (Update.isRunning()) Update.abort();
            firmwareStatus_ = 400;
            firmwareMessage_ = "Przerwano przesyłanie firmware.";
            Serial.printf("OTA: przerwano przesyłanie po %lu z %d bajtów.\n",
                          static_cast<unsigned long>(chunk.totalSize), server_.clientContentLength());
            break;
    }
}

void Panel::finishFirmware() {
    if (server_.header("Content-Type") != FirmwareType) {
        error(415, "Wyślij plik firmware.bin z zakładki Aktualizacja.");
        return;
    }
    if (firmwareStatus_ != 200) {
        error(firmwareStatus_, firmwareMessage_);
        return;
    }
    if (!firmwareAvailable()) {
        Update.abort();
        error(403, "Urządzenie utraciło dostęp do sieci domowej.");
        return;
    }
    if (!Update.end()) {
        Serial.printf("OTA: %s\n", Update.errorString());
        if (Update.isRunning()) Update.abort();
        error(422, "Weryfikacja nie powiodła się. Wybierz firmware.bin dla U1 Status Bar.");
        return;
    }
    Serial.println("OTA: zapisano i zweryfikowano firmware. Restart.");
    StaticJsonDocument<64> response;
    response["restarting"] = true;
    sendJson(200, response);
    firmwareRestartAt_ = millis() + 1000;
}

}  // namespace u1
