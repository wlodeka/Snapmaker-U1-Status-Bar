#include <unity.h>
#include "filament_color.h"
#include "requests.h"
#include "task_data.h"

using namespace u1;

void setUp() { Preferences::store.clear(); }
void tearDown() {}

void test_parse_hex() {
    uint32_t value;
    TEST_ASSERT_TRUE(parseHex("1bA6fF", 6, value));
    TEST_ASSERT_EQUAL_HEX32(0x1BA6FF, value);
    TEST_ASSERT_FALSE(parseHex("12G4", 4, value));
}

void test_filament_neutrals_use_rainbow() {
    TEST_ASSERT_TRUE(mapFilamentColor(0x000000).rainbow);
    TEST_ASSERT_TRUE(mapFilamentColor(0x0A0A0A).rainbow);
    TEST_ASSERT_TRUE(mapFilamentColor(0x101314).rainbow);
    TEST_ASSERT_TRUE(mapFilamentColor(0x808080).rainbow);
    TEST_ASSERT_TRUE(mapFilamentColor(0x838D8B).rainbow);
    TEST_ASSERT_TRUE(mapFilamentColor(0xE7E7E7).rainbow);
    TEST_ASSERT_FALSE(mapFilamentColor(0xE8E8E8).rainbow);
    TEST_ASSERT_FALSE(mapFilamentColor(0xFFFFFF).rainbow);
}

void test_dim_filament_retains_color() {
    const auto brown = mapFilamentColor(0x301808);
    TEST_ASSERT_FALSE(brown.rainbow);
    TEST_ASSERT_EQUAL_HEX32(0xC06020, brown.rgb);
    const auto blue = mapFilamentColor(0x183078);
    TEST_ASSERT_FALSE(blue.rainbow);
    TEST_ASSERT_EQUAL_HEX32(0x264DC0, blue.rgb);
    TEST_ASSERT_FALSE(mapFilamentColor(0x20182A).rainbow);
    TEST_ASSERT_EQUAL_HEX32(0xF4EBDF, mapFilamentColor(0xF4EBDF).rgb);
}

void test_led_boundaries() {
    TEST_ASSERT_TRUE(validLedBoundaries({9, 33, 42}));
    TEST_ASSERT_TRUE(validLedBoundaries({1, 2, 59}));
    TEST_ASSERT_FALSE(validLedBoundaries({0, 33, 42}));   // zone B would be empty
    TEST_ASSERT_FALSE(validLedBoundaries({9, 9, 42}));    // zone D would be empty
    TEST_ASSERT_FALSE(validLedBoundaries({9, 33, 60}));   // zone A would be empty
}

NetworkSettings network(const char* password, const char* ip = "192.168.1.20") {
    NetworkSettings value{"home", "", "", 7125};
    strcpy(value.wifiPassword, password);
    strcpy(value.moonrakerIp, ip);
    return value;
}

void test_valid_network() {
    TEST_ASSERT_TRUE(validNetwork(network("")));
    TEST_ASSERT_TRUE(validNetwork(network("12345678")));
    TEST_ASSERT_FALSE(validNetwork(network("1234567")));
    TEST_ASSERT_TRUE(validNetwork(network(std::string(64, 'a').c_str())));
    TEST_ASSERT_FALSE(validNetwork(network(std::string(64, 'x').c_str())));  // 64 chars must be a hex PSK
    TEST_ASSERT_TRUE(validNetwork(network("12345678", "")));
    TEST_ASSERT_FALSE(validNetwork(network("12345678", "127.0.0.1")));
    TEST_ASSERT_FALSE(validNetwork(network("12345678", "192.168.1.020")));
    auto noPort = network("12345678");
    noPort.moonrakerPort = 0;
    TEST_ASSERT_FALSE(validNetwork(noPort));
    auto noSsid = network("12345678");
    noSsid.ssid[0] = '\0';
    TEST_ASSERT_FALSE(validNetwork(noSsid));
}

void test_parse_tool() {
    TEST_ASSERT_EQUAL(0, parseTool("extruder"));
    TEST_ASSERT_EQUAL(3, parseTool("extruder3"));
    TEST_ASSERT_EQUAL(31, parseTool("extruder31"));
    TEST_ASSERT_EQUAL(-1, parseTool("extruder32"));
    TEST_ASSERT_EQUAL(-1, parseTool("extruder05"));
    TEST_ASSERT_EQUAL(-1, parseTool("extruderX"));
    TEST_ASSERT_EQUAL(-1, parseTool("heater_bed"));
    TEST_ASSERT_EQUAL(-1, parseTool(JsonString()));
}

void test_task_data_maps_logical_tools() {
    StaticJsonDocument<4096> doc;  // host slots are twice the ESP32 size
    std::string json = R"({"filament_color_rgba":["FF000080","00FF00FF","bad","0000FFFF"],)"
                       R"("extruders_used":[false,false,true,false],"extruder_map_table":[)";
    for (int i = 0; i < LogicalToolCount; ++i) json += (i ? "," : "") + std::to_string(i == 5 ? 2 : i % 4);
    json += "]}";
    TEST_ASSERT_FALSE(deserializeJson(doc, json));

    TaskData task;
    task.update(doc.as<JsonObjectConst>(), true);
    TEST_ASSERT_EQUAL_HEX32(0xFF0000, task.colors[0]);  // alpha byte dropped
    TEST_ASSERT_EQUAL_HEX8(0b1011, task.colorMask);     // head 2 had an invalid color
    TEST_ASSERT_EQUAL(1, task.resolve(1, false));       // physical heads need no map
    TEST_ASSERT_EQUAL(2, task.resolve(5, true));        // logical tool 5 -> used head 2
    TEST_ASSERT_EQUAL(-1, task.resolve(7, true));       // tool 7 maps to head 3, which is unused
    TEST_ASSERT_EQUAL(-1, task.resolve(5, false));      // map is only trusted during a job

    TaskData idle;
    idle.update(doc.as<JsonObjectConst>(), false);
    TEST_ASSERT_FALSE(idle.hasMap);
    TEST_ASSERT_EQUAL(-1, idle.resolve(5, true));
}

void test_read_network_keeps_saved_password() {
    const NetworkSettings current = network("secret123");
    StaticJsonDocument<256> doc;
    NetworkSettings next{};

    deserializeJson(doc, R"({"ssid":"home","wifiPassword":"","moonrakerIp":"192.168.1.30","openWifi":false,"moonrakerPort":7125})");
    TEST_ASSERT_TRUE(readNetwork(doc.as<JsonObjectConst>(), current, next));
    TEST_ASSERT_EQUAL_STRING("secret123", next.wifiPassword);

    next = {};
    deserializeJson(doc, R"({"ssid":"other","wifiPassword":"","moonrakerIp":"","openWifi":false,"moonrakerPort":7125})");
    TEST_ASSERT_FALSE(readNetwork(doc.as<JsonObjectConst>(), current, next));  // never reuse it for another SSID

    next = {};
    deserializeJson(doc, R"({"ssid":"home","wifiPassword":"secret123","moonrakerIp":"","openWifi":true,"moonrakerPort":7125})");
    TEST_ASSERT_FALSE(readNetwork(doc.as<JsonObjectConst>(), current, next));

    next = {};
    deserializeJson(doc, R"({"ssid":"ho\nme","wifiPassword":"","moonrakerIp":"","openWifi":true,"moonrakerPort":7125})");
    TEST_ASSERT_FALSE(readNetwork(doc.as<JsonObjectConst>(), current, next));  // control characters
}

void test_read_leds() {
    const char* zone = R"({"enabled":true,"brightness":50,"limit":80,"effect":1,"speed":45,"reverse":false,)"
                       R"("automaticColor":false,"manualRainbow":false,"manualColor":"#1ba6ff"})";
    std::string body = std::string(R"({"boundaries":[9,33,42],"interiorWhite":true,"white":2,"zones":[)") +
                       zone + "," + zone + "," + zone + "," + zone +
                       R"(],"fallback":["#000000","#ffffff","#123456","#abcdef"]})";
    StaticJsonDocument<4096> doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, body));
    LedSettings value{};
    TEST_ASSERT_TRUE(readLeds(doc.as<JsonObjectConst>(), value));
    TEST_ASSERT_EQUAL(80, value.limits[Interior]);
    TEST_ASSERT_EQUAL_HEX32(0xABCDEF, value.fallback[3]);

    doc["zones"][2]["brightness"] = 101;
    TEST_ASSERT_FALSE(readLeds(doc.as<JsonObjectConst>(), value));
}

// Byte layout of the v0.1 Wi-Fi record, as stored on devices in the field.
struct LegacyNetwork {
    char ssid[33];
    char wifiPassword[65];
    uint8_t retired[65];
    char moonrakerIp[16];
    uint16_t moonrakerPort;
};

void test_migrates_v01_network_and_removes_it() {
    LegacyNetwork old{"home", "secret123", {}, "192.168.1.30", 7126};
    Preferences().putBytes("net_v1", &old, sizeof(old));

    SettingsStore settings;
    settings.begin();
    TEST_ASSERT_EQUAL_STRING("home", settings.network().ssid);
    TEST_ASSERT_EQUAL_STRING("secret123", settings.network().wifiPassword);
    TEST_ASSERT_EQUAL(7126, settings.network().moonrakerPort);
    TEST_ASSERT_TRUE(Preferences::store.count("net_v2"));
    TEST_ASSERT_FALSE(Preferences::store.count("net_v1"));
}

void test_keeps_invalid_v01_network() {
    LegacyNetwork old{"home", "short", {}, "", 7125};
    Preferences().putBytes("net_v1", &old, sizeof(old));

    SettingsStore settings;
    settings.begin();
    TEST_ASSERT_EQUAL_STRING("", settings.network().ssid);
    TEST_ASSERT_TRUE(Preferences::store.count("net_v1"));  // not deleted when it cannot be migrated
}

void test_led_save_round_trip() {
    SettingsStore settings;
    settings.begin();
    LedSettings led = settings.led();
    led.boundaries = {5, 30, 50};
    led.limits[Right] = 10;
    TEST_ASSERT_TRUE(settings.save(led));
    led.boundaries = {5, 5, 50};
    TEST_ASSERT_FALSE(settings.save(led));

    SettingsStore reloaded;
    reloaded.begin();
    TEST_ASSERT_EQUAL(30, reloaded.led().boundaries[1]);
    TEST_ASSERT_EQUAL(10, reloaded.led().limits[Right]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parse_hex);
    RUN_TEST(test_filament_neutrals_use_rainbow);
    RUN_TEST(test_dim_filament_retains_color);
    RUN_TEST(test_led_boundaries);
    RUN_TEST(test_valid_network);
    RUN_TEST(test_parse_tool);
    RUN_TEST(test_task_data_maps_logical_tools);
    RUN_TEST(test_read_network_keeps_saved_password);
    RUN_TEST(test_read_leds);
    RUN_TEST(test_migrates_v01_network_and_removes_it);
    RUN_TEST(test_keeps_invalid_v01_network);
    RUN_TEST(test_led_save_round_trip);
    return UNITY_END();
}
