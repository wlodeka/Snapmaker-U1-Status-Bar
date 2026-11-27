#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace u1 {

enum class PrinterState : uint8_t {
    Offline, Unknown, Standby, Printing, Paused, Complete, Cancelled, Error
};

struct PrinterStatus {
    bool connected = false;
    PrinterState state = PrinterState::Offline;
    int8_t head = -1;
    float progress = -1.0f;
    bool hasFilamentColor = false;
    uint32_t filamentRgb = 0;
};

class Moonraker {
public:
    void begin();
    void setTarget(const char* ip, uint16_t port);
    const PrinterStatus& poll();

private:
    struct Target {
        char ip[16];
        uint16_t port;
        uint32_t generation;
    };
    struct Update {
        uint32_t generation;
        PrinterStatus status;
    };
    struct Worker;
    static void run(void* context);

    Target target_{};
    PrinterStatus status_;
    QueueHandle_t targets_ = nullptr;
    QueueHandle_t updates_ = nullptr;
};

}  // namespace u1
