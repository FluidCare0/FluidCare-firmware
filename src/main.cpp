// master_node_new_protocol.cpp
// Protocol codes: 200-206
// Subscribes to: be_project/master/in  (for 201, 205 from backend)

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ====== WiFi Credentials ======
const char *WIFI_SSID     = "Airtel_sahi_2825";
const char *WIFI_PASSWORD = "Air@68881";

// ====== MQTT / HiveMQ ======
const char    *MQTT_BROKER_HOST = "1e578bacd37e4198a99e7a4a28756c6e.s1.eu.hivemq.cloud";
const uint16_t MQTT_PORT        = 8883;
const char    *MQTT_CLIENT_ID   = "esp32-master-2026";
const char    *MQTT_USER        = "kanbs";
const char    *MQTT_PASS        = "Kartik@3165";

const int LED_PIN = 2;

// ====== Protocol Request Codes ======
#define REQ_NODE_ID       200
#define RES_NODE_ASSIGN   201
#define RES_NODE_CONFIRM  202
#define REQ_SENSOR_DATA   203
#define REQ_TASK_COMPLETE 204
#define RES_ERASE_FLASH   205
#define RES_ERASE_CONFIRM 206

// ====== MQTT Topics ======
#define TOPIC_MASTER_IN          "be_project/master/in"
#define TOPIC_NODE_REGISTER      "be_project/node/register"
#define TOPIC_NODE_CONFIRM_ID    "be_project/node/confirm_id"
#define TOPIC_NODE_DATA          "be_project/node/data"
#define TOPIC_NODE_TASK_COMPLETE "be_project/node/task_complete"
#define TOPIC_NODE_ERASE_CONFIRM "be_project/node/erase_confirm"
#define TOPIC_DISCONNECT         "be_project/disconnect"

// ========== ROOT CA CERTIFICATE ==========
static const char ISRG_ROOT_X1[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbTANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)EOF";

WiFiClientSecure espClientSecure;
PubSubClient    mqttClient(espClientSecure);

// ====== Shared packet structure (must match node) ======
typedef struct sensor_data {
  uint16_t request_code;
  char     node_id[37];
  char     node_mac[18];
  float    reading;
  float    battery_percent;
  uint32_t timestamp;
  char     date_str[11];
  char     time_str[9];
  uint8_t  via;
  char     repeater_mac[18];
  char     master_mac[18];
} sensor_data_t;

// ====== FreeRTOS queue for sensor readings ======
#define QUEUE_SIZE 10
QueueHandle_t mqttQueue;

// ====== Peer tracking ======
#define MAX_NODES 10
uint8_t registeredNodes[MAX_NODES][6];
int     nodeCount = 0;

// ===============================================================
// LED helpers
// ===============================================================
void ledBlink(unsigned long intervalMs) {
  static unsigned long lastToggle = 0;
  static bool state = false;
  unsigned long now = millis();
  if (now - lastToggle >= intervalMs) {
    lastToggle = now;
    state = !state;
    digitalWrite(LED_PIN, state ? HIGH : LOW);
  }
}
void ledOn()  { digitalWrite(LED_PIN, HIGH); }
void ledOff() { digitalWrite(LED_PIN, LOW);  }

// ===============================================================
// ESP-NOW: add peer if not already tracked
// ===============================================================
bool addPeerIfNeeded(const uint8_t *mac) {
  for (int i = 0; i < nodeCount; i++) {
    if (memcmp(registeredNodes[i], mac, 6) == 0) return true;
  }
  if (esp_now_is_peer_exist(mac)) return true;

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mac, 6);
  peerInfo.channel = 5;
  peerInfo.encrypt = false;

  esp_err_t result = esp_now_add_peer(&peerInfo);
  if (result == ESP_OK) {
    if (nodeCount < MAX_NODES) {
      memcpy(registeredNodes[nodeCount++], mac, 6);
    }
    Serial.printf("✅ Added ESP-NOW peer: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
  }
  Serial.printf("❌ Failed to add peer: %d\n", result);
  return false;
}

// ===============================================================
// Helper: parse "AA:BB:CC:DD:EE:FF" → byte array
// ===============================================================
bool parseMacString(const char *macStr, uint8_t *out) {
  return sscanf(macStr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                &out[0], &out[1], &out[2], &out[3], &out[4], &out[5]) == 6;
}

// ===============================================================
// Helper: send ESP-NOW packet with up to 3 retries
// ===============================================================
void espnowSendWithRetry(const uint8_t *mac, const sensor_data_t &pkt) {
  for (int i = 0; i < 3; i++) {
    esp_err_t r = esp_now_send(mac, (const uint8_t *)&pkt, sizeof(pkt));
    if (r == ESP_OK) {
      Serial.printf("✅ ESP-NOW sent (attempt %d)\n", i + 1);
      return;
    }
    Serial.printf("⚠️ ESP-NOW send failed (attempt %d): %d\n", i + 1, r);
    delay(100);
  }
}

// ===============================================================
// MQTT callback — receives 201 and 205 from backend
// ===============================================================
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, payload, length)) {
    Serial.println("❌ JSON parse error in MQTT callback");
    return;
  }

  uint16_t   reqCode = doc["request_code"];
  const char *macStr = doc["mac"];

  Serial.printf("📩 MQTT msg topic=%s req=%d mac=%s\n",
                topic, reqCode, macStr ? macStr : "null");

  // ---- 201: Node ID assigned → forward to node ----
  if (reqCode == RES_NODE_ASSIGN) {
    if (!macStr) { Serial.println("❌ No MAC in 201"); return; }

    uint8_t nodeMac[6];
    if (!parseMacString(macStr, nodeMac)) {
      Serial.printf("❌ Invalid MAC in 201: %s\n", macStr);
      return;
    }
    if (!addPeerIfNeeded(nodeMac)) return;

    sensor_data_t reply = {};
    reply.request_code = RES_NODE_ASSIGN;
    const char *nid = doc["node_id"] | "";
    strncpy(reply.node_id,  nid, sizeof(reply.node_id) - 1);
    strncpy(reply.node_mac, "MASTER", sizeof(reply.node_mac) - 1);

    Serial.printf("📡 Forwarding 201 → %02X:%02X:%02X:%02X:%02X:%02X  id=%s\n",
                  nodeMac[0], nodeMac[1], nodeMac[2], nodeMac[3], nodeMac[4], nodeMac[5], nid);
    espnowSendWithRetry(nodeMac, reply);
  }

  // ---- 205: Erase flash → forward to node ----
  else if (reqCode == RES_ERASE_FLASH) {
    if (!macStr) { Serial.println("❌ No MAC in 205"); return; }

    uint8_t nodeMac[6];
    if (!parseMacString(macStr, nodeMac)) {
      Serial.printf("❌ Invalid MAC in 205: %s\n", macStr);
      return;
    }
    if (!addPeerIfNeeded(nodeMac)) return;

    sensor_data_t reply = {};
    reply.request_code = RES_ERASE_FLASH;
    strncpy(reply.node_mac, "MASTER", sizeof(reply.node_mac) - 1);

    Serial.printf("📡 Forwarding 205 → %02X:%02X:%02X:%02X:%02X:%02X\n",
                  nodeMac[0], nodeMac[1], nodeMac[2], nodeMac[3], nodeMac[4], nodeMac[5]);
    espnowSendWithRetry(nodeMac, reply);
  }
}

// ===============================================================
// WiFi connect
// ===============================================================
void connectWiFi() {
  Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.config(IPAddress(192, 168, 1, 55), IPAddress(192, 168, 1, 1),
              IPAddress(255, 255, 255, 0), IPAddress(1, 1, 1, 1),
              IPAddress(8, 8, 8, 8));
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED) {
    ledBlink(300);
    delay(500);
    Serial.print(".");
    if (++retry > 40) {
      Serial.println("\n❌ WiFi connect failed, restarting...");
      ESP.restart();
    }
  }
  Serial.printf("\n✅ WiFi Connected  IP: %s\n", WiFi.localIP().toString().c_str());

  uint8_t ch; wifi_second_chan_t sch;
  esp_wifi_get_channel(&ch, &sch);
  Serial.printf("📡 WiFi channel: %d\n", ch);
}

// ===============================================================
// MQTT connect
// ===============================================================
void connectMQTT() {
  espClientSecure.setCACert(ISRG_ROOT_X1);
  mqttClient.setBufferSize(1024);
  mqttClient.setServer(MQTT_BROKER_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  Serial.println("🔐 Connecting to HiveMQ...");
  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
    Serial.println("✅ MQTT connected");
    ledOn();
    // Subscribe to the SINGLE topic for backend→master commands
    mqttClient.subscribe(TOPIC_MASTER_IN);
    Serial.printf("📬 Subscribed to %s\n", TOPIC_MASTER_IN);
  } else {
    Serial.printf("❌ MQTT failed rc=%d, restarting...\n", mqttClient.state());
    delay(4000);
    ESP.restart();
  }
}

// ===============================================================
// ESP-NOW receive callback — handles 200, 202, 203, 204, 206
// ===============================================================
void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  sensor_data_t data;
  memcpy(&data, incomingData, sizeof(data));

  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  switch (data.request_code) {

    // ---- 200: Node ID request → publish to backend ----
    case REQ_NODE_ID: {
      Serial.printf("📨 Node ID request (200) from %s\n", macStr);
      addPeerIfNeeded(mac);

      StaticJsonDocument<128> doc;
      doc["mac"]          = macStr;
      doc["request_code"] = REQ_NODE_ID;
      char payload[128];
      serializeJson(doc, payload);
      mqttClient.publish(TOPIC_NODE_REGISTER, payload);
      Serial.printf("📤 Published 200 to %s\n", TOPIC_NODE_REGISTER);
      break;
    }

    // ---- 202: Node confirmed Node ID → publish to backend ----
    case RES_NODE_CONFIRM: {
      Serial.printf("📨 Node ID confirmed (202) from %s id=%s\n", macStr, data.node_id);
      StaticJsonDocument<256> doc;
      doc["mac"]          = macStr;
      doc["node_id"]      = data.node_id;
      doc["request_code"] = RES_NODE_CONFIRM;
      char payload[256];
      serializeJson(doc, payload);
      mqttClient.publish(TOPIC_NODE_CONFIRM_ID, payload);
      Serial.printf("📤 Published 202 to %s\n", TOPIC_NODE_CONFIRM_ID);
      break;
    }

    // ---- 203: Sensor data → push to FreeRTOS queue ----
    case REQ_SENSOR_DATA: {
      if (xQueueSend(mqttQueue, &data, 0) != pdTRUE) {
        Serial.println("⚠️ MQTT queue full, dropping packet");
      }
      break;
    }

    // ---- 204: Task complete → publish to backend ----
    case REQ_TASK_COMPLETE: {
      Serial.printf("🔄 Task complete (204) from %s id=%s\n", macStr, data.node_id);
      addPeerIfNeeded(mac);

      StaticJsonDocument<256> doc;
      doc["mac"]          = macStr;
      doc["node_id"]      = data.node_id;
      doc["request_code"] = REQ_TASK_COMPLETE;
      char payload[256];
      serializeJson(doc, payload);
      mqttClient.publish(TOPIC_NODE_TASK_COMPLETE, payload);
      Serial.printf("📤 Published 204 to %s\n", TOPIC_NODE_TASK_COMPLETE);
      break;
    }

    // ---- 206: Erase confirmed → publish to backend ----
    case RES_ERASE_CONFIRM: {
      Serial.printf("🔄 Erase confirmed (206) from %s\n", macStr);
      StaticJsonDocument<128> doc;
      doc["mac"]          = macStr;
      doc["request_code"] = RES_ERASE_CONFIRM;
      char payload[128];
      serializeJson(doc, payload);
      mqttClient.publish(TOPIC_NODE_ERASE_CONFIRM, payload);
      Serial.printf("📤 Published 206 to %s\n", TOPIC_NODE_ERASE_CONFIRM);
      break;
    }

    default:
      Serial.printf("⚠️ Unknown ESP-NOW code: %d from %s\n", data.request_code, macStr);
      break;
  }
}

// ===============================================================
// setup()
// ===============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== FluidCare Master Node — Protocol 200-206 ===");

  pinMode(LED_PIN, OUTPUT);
  ledOff();

  mqttQueue = xQueueCreate(QUEUE_SIZE, sizeof(sensor_data_t));

  connectWiFi();

  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW init failed");
    ESP.restart();
  }
  esp_now_register_recv_cb(onDataRecv);
  Serial.println("✅ ESP-NOW initialized");

  connectMQTT();
  Serial.println("✅ Master ready\n");
}

// ===============================================================
// loop()
// ===============================================================
void loop() {
  if (!mqttClient.connected()) {
    Serial.println("⚠️ MQTT disconnected, reconnecting...");
    connectMQTT();
  }

  mqttClient.loop();

  // Drain queue → publish sensor data (203) to MQTT
  sensor_data_t d;
  while (xQueueReceive(mqttQueue, &d, 0) == pdTRUE) {
    StaticJsonDocument<512> doc;
    doc["request_code"]    = REQ_SENSOR_DATA;
    doc["node_id"]         = d.node_id;
    doc["node_mac"]        = d.node_mac;
    doc["reading"]         = d.reading;
    doc["battery_percent"] = d.battery_percent;
    doc["timestamp"]       = d.timestamp;
    doc["date"]            = d.date_str;
    doc["time"]            = d.time_str;

    char datetime_str[32];
    sprintf(datetime_str, "%s %s", d.date_str, d.time_str);
    doc["datetime"] = datetime_str;

    char payload[512];
    serializeJson(doc, payload);

    if (mqttClient.publish(TOPIC_NODE_DATA, payload)) {
      Serial.printf("📤 Published sensor data (203) from %s\n", d.node_id);
    } else {
      Serial.println("❌ Failed to publish sensor data");
    }
  }

  delay(30);
}