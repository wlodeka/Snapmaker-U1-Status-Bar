# Third-party dependencies

Both the source release and PlatformIO configuration describe the versions below.
PlatformIO downloads the toolchain and libraries during the build.

| Component | Version | License and upstream source |
| --- | --- | --- |
| Arduino-ESP32 | 2.0.17 | [LGPL 2.1](https://github.com/espressif/arduino-esp32/blob/2.0.17/LICENSE.md). See individual component notices. |
| ESP-IDF in Arduino-ESP32 | 4.4.7 | [Apache 2.0 and component notices](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/COPYRIGHT.html). [SDK version used by Arduino-ESP32](https://github.com/espressif/arduino-esp32/releases/tag/2.0.17). |
| Adafruit NeoPixel | 1.12.5 | [LGPL 3.0 or later](https://github.com/adafruit/Adafruit_NeoPixel/blob/1.12.5/Adafruit_NeoPixel.cpp) and [license text](https://github.com/adafruit/Adafruit_NeoPixel/blob/1.12.5/COPYING). |
| arduinoWebSockets | 2.7.2 | [LGPL 2.1 or later](https://github.com/Links2004/arduinoWebSockets/blob/2.7.2/src/WebSocketsClient.cpp) and [license text](https://github.com/Links2004/arduinoWebSockets/blob/2.7.2/LICENSE). The upstream package includes [libb64](https://github.com/Links2004/arduinoWebSockets/tree/2.7.2/src/libb64) and [libsha1](https://github.com/Links2004/arduinoWebSockets/tree/2.7.2/src/libsha1) with public-domain notices. |
| ArduinoJson | 6.21.5 | [MIT](https://github.com/bblanchon/ArduinoJson/blob/v6.21.5/LICENSE.txt). |
| Unity | Selected by PlatformIO for tests | [MIT](https://github.com/ThrowTheSwitch/Unity/blob/master/LICENSE.txt). Used by the tests. |

These notices cover the listed dependencies. The project has no separate
license granting reuse of its own source code at this time.

GitHub Release v1.0.0 contains source archives. Before distributing a compiled
image, use the linked license terms to prepare the applicable notices and
the build materials needed to relink the firmware with modified LGPL libraries.
