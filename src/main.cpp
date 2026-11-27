#include "leds.h"
#include "moonraker.h"
#include "network.h"
#include "settings.h"
#include "web.h"

namespace {
u1::SettingsStore settings;
u1::Network network(settings);
u1::Moonraker moonraker;
u1::Leds leds;
u1::Panel panel(settings, network, leds);
}

void setup() {
    Serial.begin(115200);
    Serial.printf("\nU1_Status_Bar v%s | flash: %lu B | PSRAM: %lu B\n",
                  u1::FirmwareVersion,
                  static_cast<unsigned long>(ESP.getFlashChipSize()),
                  static_cast<unsigned long>(ESP.getPsramSize()));
    settings.begin();
    leds.begin(settings.led());
    network.begin();
    moonraker.begin();
    panel.begin();
}

void loop() {
    network.tick(millis());
    const auto& config = settings.network();
    const auto stationIp = network.stationIp();
    const bool activeNetwork = stationIp != IPAddress() &&
                               network.update() != u1::NetworkUpdate::Pending;
    moonraker.setTarget(activeNetwork ? config.moonrakerIp : "",
                       activeNetwork ? config.moonrakerPort : 0);
    const auto& printer = moonraker.poll();
    leds.tick(millis(), settings.led(), printer);
    panel.tick(printer);
    delay(2);
}
