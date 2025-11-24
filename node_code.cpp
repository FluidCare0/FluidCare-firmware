#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Preferences.h>
#include <esp_wifi.h>

#define LED_PIN 2
#define SEND_INTERVAL 5000
#define MASTER_MAC {0x6C, 0xC8, 0x40, 0x34, 0xAF, 0xB0}
#define WIFI_CHANNEL 5

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
    d.reading = random(50, 150) / 1.37;
    d.battery_percent = random(60, 100);
    d.timestamp = millis();
    strcpy(d.date_str, "2025-11-11");
    strcpy(d.time_str, "12:00:00");
    d.via = 0;
    strcpy(d.repeater_mac, "00:00:00:00:00:00");
    strcpy(d.master_mac, "6C:C8:40:34:AF:B0");

    esp_now_send(masterMac, (uint8_t *)&d, sizeof(d));
    Serial.println("📤 Sensor data sent");
}

void requestNodeId()
{
    sensor_data_t d = {};
    d.request_code = REQ_NODE_ID;
    String macStr = WiFi.macAddress();
    strncpy(d.node_mac, macStr.c_str(), sizeof(d.node_mac));
    esp_now_send(masterMac, (uint8_t *)&d, sizeof(d));
    // Serial.println(WiFi.macAddress());

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

    // 📤 Send sensor data only when Node-ID is available
    if (nodeId != "" && millis() - lastSendTime >= SEND_INTERVAL)
    {
        lastSendTime = millis();
        sendSensorData();
    }

    delay(100);
}
