#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <esp_now.h>
#include <esp_wifi.h>

const char *WIFI_SSID = "Airtel_sahi_2825";
const char *WIFI_PASSWORD = "Air@68881";

const char *MQTT_BROKER_HOST = "1e578bacd37e4198a99e7a4a28756c6e.s1.eu.hivemq.cloud";
const uint16_t MQTT_PORT = 8883;
const char *MQTT_CLIENT_ID = "esp32-client-1";
const char *MQTT_USER = "kanbs";
const char *MQTT_PASS = "Kartik@3165";

const int LED_PIN = 2;

// ====== Request Codes ======
#define REQ_SENSOR_DATA 200
#define REQ_NODE_ID 201
#define RES_NODE_ID 202

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
PubSubClient mqttClient(espClientSecure);

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

#define QUEUE_SIZE 10
QueueHandle_t mqttQueue;

// Track registered nodes to avoid duplicate peer additions
#define MAX_NODES 10
uint8_t registeredNodes[MAX_NODES][6];
int nodeCount = 0;

// ===============================================================
// LED Helpers
// ===============================================================
void ledBlink(unsigned long intervalMs)
{
  static unsigned long lastToggle = 0;
  static bool state = false;
  unsigned long now = millis();
  if (now - lastToggle >= intervalMs)
  {
    lastToggle = now;
    state = !state;
    digitalWrite(LED_PIN, state ? HIGH : LOW);
  }
}

void ledOn() { digitalWrite(LED_PIN, HIGH); }
void ledOff() { digitalWrite(LED_PIN, LOW); }

// ===============================================================
// ESP-NOW Helper: Add peer if not already added
// ===============================================================
bool addPeerIfNeeded(const uint8_t *mac)
{
  // Check if peer already registered
  for (int i = 0; i < nodeCount; i++)
  {
    if (memcmp(registeredNodes[i], mac, 6) == 0)
    {
      return true; // Already added
    }
  }

  // Check if peer exists in ESP-NOW
  if (esp_now_is_peer_exist(mac))
  {
    return true;
  }

  // Add new peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mac, 6);
  peerInfo.channel = 5; // Match WiFi channel
  peerInfo.encrypt = false;

  esp_err_t result = esp_now_add_peer(&peerInfo);
  if (result == ESP_OK)
  {
    // Track this peer
    if (nodeCount < MAX_NODES)
    {
      memcpy(registeredNodes[nodeCount], mac, 6);
      nodeCount++;
    }
    Serial.printf("✅ Added ESP-NOW peer: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
  }
  else
  {
    Serial.printf("❌ Failed to add peer: %d\n", result);
    return false;
  }
}

// ===============================================================
// WiFi Connect
// ===============================================================
void connectWiFi()
{
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  WiFi.config(IPAddress(192, 168, 1, 55), IPAddress(192, 168, 1, 1),
              IPAddress(255, 255, 255, 0), IPAddress(1, 1, 1, 1),
              IPAddress(8, 8, 8, 8));

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    ledBlink(300);
    delay(500);
    Serial.print(".");
    if (++retry > 40)
    {
      Serial.println("\n❌ WiFi connect failed, restarting...");
      ESP.restart();
    }
  }

  Serial.println("\n✅ WiFi Connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  uint8_t ch;
  wifi_second_chan_t sch;
  esp_wifi_get_channel(&ch, &sch);
  Serial.printf("📡 Master WiFi channel: %d\n", ch);
}

// ===============================================================
// MQTT Callback (Receive Node-ID from server)
// ===============================================================
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, payload, length))
  {
    Serial.println("❌ JSON parse error");
    return;
  }

  uint16_t reqCode = doc["request_code"];
  const char *macStr = doc["mac"];
  const char *nodeId = doc["node_id"];

  Serial.printf("📩 MQTT msg: req=%d, mac=%s, node_id=%s\n",
                reqCode, macStr, nodeId);

  if (reqCode == RES_NODE_ID)
  {
    uint8_t nodeMac[6];
    int parsed = sscanf(macStr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                        &nodeMac[0], &nodeMac[1], &nodeMac[2],
                        &nodeMac[3], &nodeMac[4], &nodeMac[5]);

    if (parsed != 6)
    {
      Serial.printf("❌ Invalid MAC from MQTT: %s\n", macStr);
      return;
    }

    // Ensure peer is added before sending
    if (!addPeerIfNeeded(nodeMac))
    {
      Serial.println("❌ Cannot add peer, skipping send");
      return;
    }

    Serial.printf("📡 ESP-NOW reply → %02X:%02X:%02X:%02X:%02X:%02X\n",
                  nodeMac[0], nodeMac[1], nodeMac[2],
                  nodeMac[3], nodeMac[4], nodeMac[5]);

    sensor_data_t reply = {};
    reply.request_code = RES_NODE_ID;
    strncpy(reply.node_id, nodeId, sizeof(reply.node_id) - 1);
    strncpy(reply.node_mac, "MASTER", sizeof(reply.node_mac) - 1);

    // Send with retry
    for (int i = 0; i < 3; i++)
    {
      esp_err_t result = esp_now_send(nodeMac, (uint8_t *)&reply, sizeof(reply));
      if (result == ESP_OK)
      {
        Serial.printf("✅ Sent Node-ID to node (attempt %d)\n", i + 1);
        break;
      }
      else
      {
        Serial.printf("⚠️ Send failed (attempt %d): %d\n", i + 1, result);
        delay(100);
      }
    }
  }
}

// ===============================================================
// MQTT Connect
// ===============================================================
void connectMQTT()
{
  espClientSecure.setCACert(ISRG_ROOT_X1);

  mqttClient.setBufferSize(1024);
  mqttClient.setServer(MQTT_BROKER_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  Serial.println("🔐 Connecting to HiveMQ...");

  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS))
  {
    Serial.println("✅ MQTT connected");
    ledOn();
    mqttClient.subscribe("be_project/test/in");
    Serial.println("📬 Subscribed to: be_project/test/in");
  }
  else
  {
    Serial.printf("❌ MQTT failed, rc=%d\n", mqttClient.state());
    delay(4000);
    ESP.restart();
  }
}

// ===============================================================
// ESP-NOW Receive from Node
// ===============================================================
void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
  sensor_data_t data;
  memcpy(&data, incomingData, sizeof(data));

  if (data.request_code == REQ_SENSOR_DATA)
  {
    Serial.printf("📥 Sensor data from %s\n", data.node_mac);
    xQueueSend(mqttQueue, &data, 0);
  }
  else if (data.request_code == REQ_NODE_ID)
  {
    // Build REAL MAC string from ESP-NOW header
    char macStr[18];
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    Serial.printf("📨 Node-ID request from %s\n", macStr);

    // Add peer immediately
    addPeerIfNeeded(mac);

    StaticJsonDocument<160> doc;
    doc["mac"] = macStr;
    doc["request_code"] = REQ_NODE_ID;
    doc["type"] = "node";

    char payload[160];
    serializeJson(doc, payload);

    if (mqttClient.publish("be_project/node/request/id", payload))
    {
      Serial.printf("📤 Forwarded request to server for MAC: %s\n", macStr);
    }
    else
    {
      Serial.println("❌ MQTT publish failed!");
    }
  }
}

// ===============================================================
// Setup
// ===============================================================
void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== ESP32 Master Starting ===");

  pinMode(LED_PIN, OUTPUT);
  ledOff();

  mqttQueue = xQueueCreate(QUEUE_SIZE, sizeof(sensor_data_t));

  connectWiFi();

  // Initialize ESP-NOW AFTER WiFi is connected
  if (esp_now_init() != ESP_OK)
  {
    Serial.println("❌ ESP-NOW init failed");
    ESP.restart();
  }

  esp_now_register_recv_cb(onDataRecv);
  Serial.println("✅ ESP-NOW initialized");

  connectMQTT();
  Serial.println("✅ Master Ready\n");
}

// ===============================================================
// Loop
// ===============================================================
void loop()
{
  if (!mqttClient.connected())
  {
    Serial.println("⚠️ MQTT disconnected, reconnecting...");
    connectMQTT();
  }

  mqttClient.loop();

  if (uxQueueMessagesWaiting(mqttQueue) > 0)
  {
    sensor_data_t d;
    xQueueReceive(mqttQueue, &d, 0);

    StaticJsonDocument<512> doc;
    doc["request_code"] = REQ_SENSOR_DATA;
    doc["node_id"] = d.node_id;
    doc["node_mac"] = d.node_mac;
    doc["reading"] = d.reading;
    doc["battery_percent"] = d.battery_percent;
    doc["timestamp"] = d.timestamp;
    doc["date"] = d.date_str;
    doc["time"] = d.time_str;

    char datetime_str[32];
    sprintf(datetime_str, "%s %s", d.date_str, d.time_str);
    doc["datetime"] = datetime_str;

    char payload[512];
    serializeJson(doc, payload);

    if (mqttClient.publish("be_project/node/data", payload))
    {
      Serial.println("📤 Published sensor data");
    }
    else
    {
      Serial.println("❌ Failed to publish sensor data");
    }
  }

  delay(30);
}

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <esp_now.h>
#include <esp_wifi.h>

const char *WIFI_SSID = "Airtel_sahi_2825";
const char *WIFI_PASSWORD = "Air@68881";

const char *MQTT_BROKER_HOST = "1e578bacd37e4198a99e7a4a28756c6e.s1.eu.hivemq.cloud";
const uint16_t MQTT_PORT = 8883;
const char *MQTT_CLIENT_ID = "esp32-client-1";
const char *MQTT_USER = "kanbs";
const char *MQTT_PASS = "Kartik@3165";

const int LED_PIN = 2;

// ====== Request Codes ======
#define REQ_SENSOR_DATA 200
#define REQ_NODE_ID 201
#define RES_NODE_ID 202

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
PubSubClient mqttClient(espClientSecure);

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

#define QUEUE_SIZE 10
QueueHandle_t mqttQueue;

// ===============================================================
// LED Helpers
// ===============================================================
void ledBlink(unsigned long intervalMs)
{
  static unsigned long lastToggle = 0;
  static bool state = false;
  unsigned long now = millis();
  if (now - lastToggle >= intervalMs)
  {
    lastToggle = now;
    state = !state;
    digitalWrite(LED_PIN, state ? HIGH : LOW);
  }
}

void ledOn() { digitalWrite(LED_PIN, HIGH); }
void ledOff() { digitalWrite(LED_PIN, LOW); }

// ===============================================================
// WiFi Connect
// ===============================================================
void connectWiFi()
{
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  WiFi.config(IPAddress(192, 168, 1, 55), IPAddress(192, 168, 1, 1),
              IPAddress(255, 255, 255, 0), IPAddress(1, 1, 1, 1),
              IPAddress(8, 8, 8, 8));

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    ledBlink(300);
    delay(500);
    Serial.print(".");
    if (++retry > 40)
    {
      Serial.println("\n❌ WiFi connect failed, restarting...");
      ESP.restart();
    }
  }

  Serial.println("\n✅ WiFi Connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  uint8_t ch;
  wifi_second_chan_t sch;
  esp_wifi_get_channel(&ch, &sch);
  Serial.printf("📡 Master WiFi channel: %d\n", ch);
}

// ===============================================================
// MQTT Callback (Receive Node-ID from server)
// ===============================================================
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, payload, length))
  {
    Serial.println("❌ JSON parse error");
    return;
  }

  uint16_t reqCode = doc["request_code"];
  const char *macStr = doc["mac"];
  const char *nodeId = doc["node_id"];

  Serial.printf("📩 MQTT msg: req=%d, mac=%s, node_id=%s\n",
                reqCode, macStr, nodeId);

  if (reqCode == RES_NODE_ID)
  {

    uint8_t nodeMac[6];
    int parsed = sscanf(macStr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                        &nodeMac[0], &nodeMac[1], &nodeMac[2],
                        &nodeMac[3], &nodeMac[4], &nodeMac[5]);

    if (parsed != 6)
    {
      Serial.printf("❌ Invalid MAC from MQTT: %s\n", macStr);
      return;
    }

    Serial.printf("📡 ESP-NOW reply → %02X:%02X:%02X:%02X:%02X:%02X\n",
                  nodeMac[0], nodeMac[1], nodeMac[2],
                  nodeMac[3], nodeMac[4], nodeMac[5]);

    sensor_data_t reply = {};
    reply.request_code = RES_NODE_ID;
    strncpy(reply.node_id, nodeId, sizeof(reply.node_id));
    strncpy(reply.node_mac, "MASTER", sizeof(reply.node_mac));

    esp_now_send(nodeMac, (uint8_t *)&reply, sizeof(reply));
    Serial.println("✅ Sent Node ID to respective node");
  }
}

// ===============================================================
// MQTT Connect
// ===============================================================
void connectMQTT()
{
  espClientSecure.setCACert(ISRG_ROOT_X1);

  mqttClient.setBufferSize(1024);
  mqttClient.setServer(MQTT_BROKER_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  Serial.println("🔐 Connecting to HiveMQ...");

  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS))
  {
    Serial.println("✅ MQTT connected");
    ledOn();
    mqttClient.subscribe("be_project/test/in");
  }
  else
  {
    Serial.printf("❌ MQTT failed, rc=%d\n", mqttClient.state());
    delay(4000);
    ESP.restart();
  }
}

// ===============================================================
// ESP-NOW Receive from Node
// ===============================================================
void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
  sensor_data_t data;
  memcpy(&data, incomingData, sizeof(data));

  if (data.request_code == REQ_SENSOR_DATA)
  {
    Serial.printf("📥 Sensor data from %s\n", data.node_mac);
    xQueueSend(mqttQueue, &data, 0);
  }
  else if (data.request_code == REQ_NODE_ID)
  {

    // Build REAL MAC string from ESP-NOW header
    char macStr[18];
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    Serial.printf("📨 Node-ID request from %s\n", macStr);

    StaticJsonDocument<160> doc;
    doc["mac"] = macStr; // send REAL MAC to server
    doc["request_code"] = REQ_NODE_ID;
    doc["type"] = "node";

    char payload[160];
    serializeJson(doc, payload);

    mqttClient.publish("be_project/node/request/id", payload);
    Serial.printf("📤 Forwarded request to server for MAC: %s\n", macStr);
  }
}

// ===============================================================
// Setup
// ===============================================================
void setup()
{
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  ledOff();

  mqttQueue = xQueueCreate(QUEUE_SIZE, sizeof(sensor_data_t));

  connectWiFi();

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("❌ ESP-NOW init failed");
    ESP.restart();
  }

  esp_now_register_recv_cb(onDataRecv);

  connectMQTT();
  Serial.println("✅ Master Ready");
}

// ===============================================================
// Loop
// ===============================================================
void loop()
{

  if (!mqttClient.connected())
    connectMQTT();

  mqttClient.loop();

  if (uxQueueMessagesWaiting(mqttQueue) > 0)
  {
    sensor_data_t d;
    xQueueReceive(mqttQueue, &d, 0);

    StaticJsonDocument<512> doc;
    doc["request_code"] = REQ_SENSOR_DATA;
    doc["node_id"] = d.node_id;
    doc["node_mac"] = d.node_mac;
    doc["reading"] = d.reading;
    doc["battery_percent"] = d.battery_percent;
    doc["timestamp"] = d.timestamp;
    doc["date"] = d.date_str;
    doc["time"] = d.time_str;

    char datetime_str[32];
    sprintf(datetime_str, "%s %s", d.date_str, d.time_str);
    doc["datetime"] = datetime_str;

    char payload[512];
    serializeJson(doc, payload);

    mqttClient.publish("be_project/node/data", payload);

    Serial.println("📤 Published sensor data");
  }

  delay(30);
}