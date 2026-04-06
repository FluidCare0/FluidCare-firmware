#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <HX711.h>
#include <algorithm>

// ================= CONFIG =================
#define LED_PIN 2
#define BUTTON_PIN 0
#define SEND_INTERVAL 1000
#define HEARTBEAT_INTERVAL 5000

#define MASTER_MAC {0x6C, 0xC8, 0x40, 0x34, 0xAF, 0xB0}
#define WIFI_CHANNEL 5

#define HX711_DT 4
#define HX711_SCK 5

#define REQ_SENSOR_DATA 200
#define REQ_NODE_ID 201
#define RES_NODE_ID 202

// ===== Calibration (NEW) =====
#define CALIB_M 0.00119454f
#define CALIB_B 682.7990f

// ===== Filtering =====
#define BUF_SIZE 10
#define DELTA_THRESHOLD 1.0f

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

// ================= GLOBALS =================
Preferences preferences;
uint8_t masterMac[] = MASTER_MAC;

HX711 scale;
String nodeId = "";

unsigned long lastSendTime = 0;
unsigned long lastHeartbeat = 0;

int32_t rawBuf[BUF_SIZE];
int bufIndex = 0;
int bufCount = 0;

float lastSentWeight = -9999;

// ================= UTILS =================

float rawToGrams(int32_t raw)
{
  return CALIB_M * raw + CALIB_B;
}

void sampleLoadCell()
{
  if (!scale.is_ready())
    return;

  int32_t raw = scale.read();
  rawBuf[bufIndex] = raw;
  bufIndex = (bufIndex + 1) % BUF_SIZE;
  if (bufCount < BUF_SIZE)
    bufCount++;
}

float getFilteredWeight()
{
  if (bufCount < 3)
    return NAN;

  int32_t tmp[BUF_SIZE];
  memcpy(tmp, rawBuf, sizeof(int32_t) * bufCount);
  std::sort(tmp, tmp + bufCount);

  float q1 = tmp[bufCount / 4];
  float q3 = tmp[(3 * bufCount) / 4];
  float iqr = q3 - q1;

  float low = q1 - 1.5 * iqr;
  float high = q3 + 1.5 * iqr;

  int32_t filtered[BUF_SIZE];
  int count = 0;

  for (int i = 0; i < bufCount; i++)
  {
    if (tmp[i] >= low && tmp[i] <= high)
      filtered[count++] = tmp[i];
  }

  if (count == 0)
    return rawToGrams(tmp[bufCount / 2]);

  return rawToGrams(filtered[count / 2]);
}

// ================= ESP CALLBACKS =================

void OnDataSent(const uint8_t *, esp_now_send_status_t st)
{
  Serial.printf("📡 ESP-NOW send %s\n", st == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

void OnDataRecv(const uint8_t *, const uint8_t *incoming, int)
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
    Serial.println("💾 Node-ID saved");
  }
}

// ================= CORE =================

void sendSensorData(bool forceSend = false)
{
  if (nodeId == "")
    return;

  float weight = getFilteredWeight();
  if (isnan(weight))
    return;

  bool shouldSend = false;

  if (fabs(weight - lastSentWeight) > DELTA_THRESHOLD)
    shouldSend = true;

  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL)
  {
    shouldSend = true;
    lastHeartbeat = millis();
  }

  if (!shouldSend && !forceSend)
    return;

  sensor_data_t d = {};
  d.request_code = REQ_SENSOR_DATA;

  strncpy(d.node_id, nodeId.c_str(), sizeof(d.node_id));
  String macStr = WiFi.macAddress();
  strncpy(d.node_mac, macStr.c_str(), sizeof(d.node_mac));

  d.reading = weight;
  d.battery_percent = random(60, 100);
  d.timestamp = millis();

  strcpy(d.date_str, "2025-11-11");
  strcpy(d.time_str, "12:00:00");
  d.via = 0;
  strcpy(d.repeater_mac, "00:00:00:00:00:00");
  strcpy(d.master_mac, "6C:C8:40:34:AF:B0");

  esp_now_send(masterMac, (uint8_t *)&d, sizeof(d));

  lastSentWeight = weight;

  Serial.print("📤 Sent: ");
  Serial.println(weight);
}

void requestNodeId()
{
  sensor_data_t d = {};
  d.request_code = REQ_NODE_ID;

  String macStr = WiFi.macAddress();
  strncpy(d.node_mac, macStr.c_str(), sizeof(d.node_mac));

  esp_now_send(masterMac, (uint8_t *)&d, sizeof(d));
  Serial.println("📨 Request Node-ID");
}

// ================= BUTTON =================

void handleButton()
{
  static unsigned long pressTime = 0;
  static bool pressed = false;

  bool state = digitalRead(BUTTON_PIN) == LOW;

  if (state && !pressed)
  {
    pressTime = millis();
    pressed = true;
  }
  else if (!state && pressed)
  {
    pressed = false;

    if (millis() - pressTime > 2000)
    {
      Serial.println("🔴 Long press → Reset Node-ID");

      preferences.begin("node_config", false);
      preferences.clear();
      preferences.end();

      nodeId = "";
      lastSentWeight = -9999;
    }
    else
    {
      requestNodeId();
    }
  }
}

// ================= SETUP =================

void setup()
{
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  preferences.begin("node_config", false);
  nodeId = preferences.getString("node_id", "");
  preferences.end();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  esp_wifi_start();
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

  esp_now_init();
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, masterMac, 6);
  peer.channel = WIFI_CHANNEL;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  scale.begin(HX711_DT, HX711_SCK);

  Serial.println("✅ System Ready");

  if (nodeId == "")
    requestNodeId();
}

// ================= LOOP =================

void loop()
{
  static unsigned long lastReq = 0;

  handleButton();
  sampleLoadCell();

  if (nodeId == "" && millis() - lastReq > 10000)
  {
    lastReq = millis();
    requestNodeId();
  }

  if (nodeId != "" && millis() - lastSendTime >= SEND_INTERVAL)
  {
    lastSendTime = millis();
    sendSensorData();
  }

  delay(100);
}