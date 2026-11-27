#include "moonraker.h"
#include "settings.h"
#include "task_data.h"
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <math.h>

namespace u1 {
namespace {

constexpr char CoreObjects[] =
    R"("print_stats":["state"],"toolhead":["extruder"],"virtual_sdcard":["progress"])";
constexpr char TaskObject[] =
    R"(,"print_task_config":["filament_color_rgba","extruder_map_table","extruders_used"])";

class LocalWebSocket : public WebSocketsClient {
public:
    void loop() {
        WebSocketsClient::loop();
        // Arduino-ESP32 2.x WiFiClient takes seconds here; WebSockets passes milliseconds.
        // The TCP connect timeout still uses milliseconds.
        if (_client.status == WSC_HEADER && _client.tcp) _client.tcp->setTimeout(1);
    }
};

bool isActiveJob(PrinterState state) {
    return state == PrinterState::Printing || state == PrinterState::Paused;
}

PrinterState parseState(const char* state) {
    if (!state) return PrinterState::Unknown;
    if (strcmp(state, "standby") == 0) return PrinterState::Standby;
    if (strcmp(state, "printing") == 0) return PrinterState::Printing;
    if (strcmp(state, "paused") == 0) return PrinterState::Paused;
    if (strcmp(state, "complete") == 0) return PrinterState::Complete;
    if (strcmp(state, "cancelled") == 0) return PrinterState::Cancelled;
    if (strcmp(state, "error") == 0) return PrinterState::Error;
    return PrinterState::Unknown;
}

}  // namespace

struct Moonraker::Worker {
    explicit Worker(Moonraker& owner) : owner(owner) {}

    Moonraker& owner;
    LocalWebSocket socket;
    Target target{};
    PrinterStatus status;
    TaskData task;
    StaticJsonDocument<3072> message;
    char fragments[4096];
    size_t fragmentSize = 0;
    bool collecting = false;
    bool initialized = false;
    bool includeTask = true;
    bool requestSubscription = false;
    uint32_t requestSequence = 0;
    uint32_t pending = 0;
    uint32_t sentAt = 0;
    uint32_t requestAt = 0;
    int tool = -1;

    void publish() {
        const Update update{target.generation, status};
        xQueueOverwrite(owner.updates_, &update);
    }

    void reset(bool connected, PrinterState state = PrinterState::Offline) {
        status = {};
        status.connected = connected;
        status.state = state;
        task = {};
        tool = -1;
        pending = 0;
        initialized = false;
        includeTask = true;
        requestSubscription = connected;
        requestAt = millis();
        publish();
    }

    void setTarget(const Target& value) {
        socket.disconnect();
        target = value;
        reset(false);
        if (!target.ip[0]) return;
        socket.begin(target.ip, target.port, "/websocket", "");
        socket.setExtraHeaders();
        socket.setReconnectInterval(4000);
        socket.enableHeartbeat(10000, 3000, 2);
    }

    void subscribe(uint32_t now) {
        char body[320];
        if (++requestSequence == 0) ++requestSequence;
        pending = requestSequence;
        sentAt = now;
        requestSubscription = false;
        snprintf(body, sizeof(body),
                 R"({"jsonrpc":"2.0","id":%lu,"method":"printer.objects.subscribe","params":{"objects":{%s%s}}})",
                 static_cast<unsigned long>(pending), CoreObjects, includeTask ? TaskObject : "");
        if (!socket.sendTXT(body)) socket.disconnect();
    }

    void apply(JsonObjectConst data, bool snapshot) {
        const auto stats = data["print_stats"].as<JsonObjectConst>();
        const bool wasActive = isActiveJob(status.state);
        if (stats.containsKey("state") || snapshot) {
            status.state = parseState(stats["state"].as<const char*>());
        }
        const bool active = isActiveJob(status.state);
        if (snapshot) task = {};
        if (active != wasActive) {
            task.usedMask = 0;
            task.hasMap = false;
            if (active) status.progress = -1.0f;
            if (active && !snapshot && includeTask) {
                // Task setup can precede the first printing delta. Re-read one full snapshot.
                task.colorMask = 0;
                requestAt = millis();
                requestSubscription = true;
            }
        }
        const auto toolhead = data["toolhead"].as<JsonObjectConst>();
        if (toolhead.containsKey("extruder") || snapshot) {
            tool = parseTool(toolhead["extruder"].as<JsonString>());
        }
        const auto sd = data["virtual_sdcard"].as<JsonObjectConst>();
        if (sd.containsKey("progress") || snapshot) {
            const auto value = sd["progress"];
            const float progress = value.as<float>();
            status.progress = value.is<float>() && isfinite(progress) &&
                              progress >= 0.0f && progress <= 1.0f ? progress : -1.0f;
        }
        if (status.state == PrinterState::Complete) status.progress = 1.0f;
        task.update(data["print_task_config"].as<JsonObjectConst>(), active);
        status.head = task.resolve(tool, active);
        status.hasFilamentColor = status.head >= 0 && (task.colorMask & (1u << status.head));
        status.filamentRgb = status.hasFilamentColor ? task.colors[status.head] : 0;
        publish();
    }

    void onMessage(char* bytes, size_t length) {
        if (!length || length > sizeof(fragments) ||
            deserializeJson(message, bytes, length, DeserializationOption::NestingLimit(8))) return;
        const JsonObjectConst envelope = message.as<JsonObjectConst>();
        if (pending && envelope["id"].is<uint32_t>() && envelope["id"].as<uint32_t>() == pending) {
            pending = 0;
            const auto data = envelope["result"]["status"].as<JsonObjectConst>();
            if (!data["print_stats"]["state"].is<const char*>()) {
                includeTask = !includeTask;
                requestAt = millis() + 3000;
                requestSubscription = true;
                return;
            }
            initialized = true;
            apply(data, true);
            return;
        }

        const char* method = envelope["method"] | "";
        if (strcmp(method, "notify_klippy_disconnected") == 0 ||
            strcmp(method, "notify_klippy_ready") == 0) {
            reset(true);
        } else if (strcmp(method, "notify_klippy_shutdown") == 0) {
            reset(true, PrinterState::Error);
            requestSubscription = false;
        } else if (initialized && strcmp(method, "notify_status_update") == 0) {
            const auto delta = envelope["params"][0].as<JsonObjectConst>();
            if (!delta.isNull()) apply(delta, false);
        }
    }

    void onSocket(WStype_t type, uint8_t* bytes, size_t length) {
        switch (type) {
            case WStype_CONNECTED:
            case WStype_DISCONNECTED:
                collecting = false;
                reset(type == WStype_CONNECTED);
                break;
            case WStype_TEXT:
                onMessage(reinterpret_cast<char*>(bytes), length);
                break;
            case WStype_FRAGMENT_TEXT_START:
                collecting = true;
                fragmentSize = 0;
                // fall through
            case WStype_FRAGMENT:
            case WStype_FRAGMENT_FIN:
                if (!collecting || length > sizeof(fragments) - fragmentSize) {
                    collecting = false;
                    break;
                }
                if (length) memcpy(fragments + fragmentSize, bytes, length);
                fragmentSize += length;
                if (type == WStype_FRAGMENT_FIN) {
                    collecting = false;
                    onMessage(fragments, fragmentSize);
                }
                break;
            default:
                break;
        }
    }

    void tick() {
        Target next;
        if (xQueueReceive(owner.targets_, &next, 0) == pdPASS) setTarget(next);
        if (!target.ip[0]) return;
        socket.loop();
        if (!status.connected) return;
        const uint32_t now = millis();
        if (pending && now - sentAt >= 7000) socket.disconnect();
        else if (!pending && requestSubscription && static_cast<int32_t>(now - requestAt) >= 0) {
            subscribe(now);
        }
    }
};

void Moonraker::run(void* context) {
    // The WebSocket client performs synchronous TCP I/O. Only this worker waits for it.
    static Worker worker(*static_cast<Moonraker*>(context));
    worker.socket.onEvent([](WStype_t type, uint8_t* bytes, size_t length) {
        worker.onSocket(type, bytes, length);
    });
    for (;;) {
        worker.tick();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void Moonraker::begin() {
    targets_ = xQueueCreate(1, sizeof(Target));
    updates_ = xQueueCreate(1, sizeof(Update));
    if (targets_ && updates_ &&
        xTaskCreatePinnedToCore(run, "moonraker", 6144, this, 1, nullptr, 0) == pdPASS) return;
    if (targets_) vQueueDelete(targets_);
    if (updates_) vQueueDelete(updates_);
    targets_ = updates_ = nullptr;
    Serial.println("Moonraker: błąd uruchomienia zadania sieciowego.");
}

void Moonraker::setTarget(const char* ip, uint16_t port) {
    if (!targets_ || (target_.port == port && strcmp(target_.ip, ip) == 0)) return;
    strlcpy(target_.ip, ip, sizeof(target_.ip));
    target_.port = port;
    ++target_.generation;
    status_ = {};
    xQueueOverwrite(targets_, &target_);
}

const PrinterStatus& Moonraker::poll() {
    Update update;
    if (updates_ && xQueueReceive(updates_, &update, 0) == pdPASS &&
        update.generation == target_.generation) status_ = update.status;
    return status_;
}

}  // namespace u1
