#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <HX711.h> // NEW

#define LED_PIN 2
// #define SEND_INTERVAL 5000   // OLD: send every 5 seconds
#define SEND_INTERVAL 1000 // NEW: send every 1 second
#define MASTER_MAC {0x6C, 0xC8, 0x40, 0x34, 0xAF, 0xB0}
#define WIFI_CHANNEL 5

// ===== HX711 PINS =====
#define HX711_DT 4
#define HX711_SCK 5

// ===== Request Codes =====
#define REQ_SENSOR_DATA 200
#define REQ_NODE_ID 201
#define RES_NODE_ID 202

typedef struct sensor_data
{
    uint16_t request_code;
    char node_id[37];
    char node_mac[18];
    float reading;
    float battery_percent;
    uint32_t timestamp;
    char date_str[11];
    char time_str[9];
    uint8_t via;
    char repeater_mac[18];
    char master_mac[18];
} sensor_data_t;

Preferences preferences;
uint8_t masterMac[] = MASTER_MAC;
String nodeId;
unsigned long lastSendTime = 0;

// ===== HX711 OBJECT =====
HX711 scale;
// float calibration_factor = -7050;  // OLD: incorrect calibration factor
float calibration_factor = -415.8; // NEW: calculated from 2-point calibration

// ===== ML CONVERSION (COMMENTED) =====
// float density = 1.000;  // Water
// float density = 1.030;  // Milk
// float density = 1.045;  // Juice
// float density = 1.420;  // Honey
// float density = 1.000;  // DEFAULT: water (1g = 1ml)

void blinkLED(int t, int d)
{
    for (int i = 0; i < t; i++)
    {
        digitalWrite(LED_PIN, HIGH);
        delay(d);
        digitalWrite(LED_PIN, LOW);
        delay(d);
    }
}

void OnDataSent(const uint8_t *, esp_now_send_status_t st)
{
    Serial.printf("📡 ESP-NOW send %s\n", (st == ESP_NOW_SEND_SUCCESS) ? "✅ OK" : "❌ FAIL");
}

void OnDataRecv(const uint8_t *mac, const uint8_t *incoming, int len)
{
    sensor_data_t data;
    memcpy(&data, incoming, sizeof(data));
    if (data.request_code == RES_NODE_ID)
    {
        Serial.printf("📩 Node-ID received: %s\n", data.node_id);
        preferences.begin("node_config", false);
        preferences.putString("node_id", data.node_id);
        preferences.end();
        nodeId = data.node_id;
        Serial.println("💾 Node-ID saved to flash");
    }
}

// ===== READ LOAD CELL IN GRAMS =====
float readWeight()
{
    if (!scale.is_ready())
    {
        Serial.println("⚠ HX711 not ready");
        return 0;
    }

    // float weight = scale.get_units(10);  // OLD: averages 10 readings (~1s delay)
    float weight = scale.get_units(1); // NEW: 1 reading for faster 1Hz sending

    // ===== ML CONVERSION (COMMENTED) =====
    // float volume = weight / density;     // Convert grams to ML using density
    // Serial.print("⚖ Volume: ");
    // Serial.print(volume);
    // Serial.println(" ml");
    // return volume;

    Serial.print("⚖ Weight: ");
    Serial.print(weight);
    Serial.println(" g");

    return weight; // Returns grams
}

void sendSensorData()
{
    if (nodeId == "")
    {
        Serial.println("⚠️ No Node-ID, cannot send sensor data");
        return;
    }

    sensor_data_t d = {};
    d.request_code = REQ_SENSOR_DATA;
    strncpy(d.node_id, nodeId.c_str(), sizeof(d.node_id));
    String macStr = WiFi.macAddress();
    strncpy(d.node_mac, macStr.c_str(), sizeof(d.node_mac));

    // ===== OLD FAKE DATA (COMMENTED) =====
    // d.reading = random(50, 150) / 1.37;

    // ===== NEW REAL LOAD CELL DATA (grams) =====
    d.reading = readWeight();

    d.battery_percent = random(60, 100);
    d.timestamp = millis();
    strcpy(d.date_str, "2025-11-11");
    strcpy(d.time_str, "12:00:00");
    d.via = 0;
    strcpy(d.repeater_mac, "00:00:00:00:00:00");
    strcpy(d.master_mac, "6C:C8:40:34:AF:B0");

    esp_now_send(masterMac, (uint8_t *)&d, sizeof(d));
    Serial.println("📤 Sensor data sent (g)");
}

void requestNodeId()
{
    sensor_data_t d = {};
    d.request_code = REQ_NODE_ID;
    String macStr = WiFi.macAddress();
    strncpy(d.node_mac, macStr.c_str(), sizeof(d.node_mac));
    esp_now_send(masterMac, (uint8_t *)&d, sizeof(d));

    Serial.println("📨 Requested Node-ID from master");
}

void setup()
{
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    preferences.begin("node_config", false);
    nodeId = preferences.getString("node_id", "");
    preferences.end();

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_start();
    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK)
    {
        Serial.println("❌ ESP-NOW init failed");
        while (1)
            delay(1000);
    }
    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, masterMac, 6);
    peer.channel = WIFI_CHANNEL;
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    // ===== HX711 INITIALIZATION =====
    Serial.println("⚖ Initializing Load Cell...");
    scale.begin(HX711_DT, HX711_SCK);
    scale.set_scale(calibration_factor);
    delay(2000);
    scale.tare();
    Serial.println("✅ Load Cell Ready (g mode)");

    if (nodeId == "")
    {
        Serial.println("⚠️ No Node-ID → sending request");
        requestNodeId();
    }
    else
        Serial.printf("✅ Existing Node-ID: %s\n", nodeId.c_str());
}

void loop()
{
    static unsigned long lastReq = 0;

    // 🔁 Retry Node-ID request every 10 s until received
    if (nodeId == "" && millis() - lastReq > 10000)
    {
        lastReq = millis();
        requestNodeId();
    }

    // 📤 Send sensor data every 1 second when Node-ID is available
    if (nodeId != "" && millis() - lastSendTime >= SEND_INTERVAL)
    {
        lastSendTime = millis();
        sendSensorData();
    }

    delay(100);
}