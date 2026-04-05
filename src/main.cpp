#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ================= WiFi =================
const char *WIFI_SSID = "Airtel_sahi_2825";
const char *WIFI_PASSWORD = "Air@68881";

// ================= MQTT =================
const char *MQTT_BROKER_HOST = "1e578bacd37e4198a99e7a4a28756c6e.s1.eu.hivemq.cloud";
const uint16_t MQTT_PORT = 8883;
const char *MQTT_CLIENT_ID = "esp32-client-1";
const char *MQTT_USER = "kanbs";
const char *MQTT_PASS = "Kartik@3165";

// ================= GPIO =================
const int LED_PIN = 2;

// ================= Request Codes =================
#define REQ_SENSOR_DATA 200
#define REQ_NODE_ID 201
#define RES_NODE_ID 202

// ================= Root CA =================
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
...
-----END CERTIFICATE-----
)EOF";

// ================= Objects =================
WiFiClientSecure espClientSecure;
PubSubClient mqttClient(espClientSecure);

typedef struct
{
  uint16_t request_code;
  char node_id[37];
  char node_mac[18];
  float reading;
  float battery_percent;
  uint32_t timestamp;
  char date_str[11];
  char time_str[9];
} sensor_data_t;

#define QUEUE_SIZE 10
QueueHandle_t mqttQueue;

// ================= LED =================
void ledBlink(unsigned long intervalMs)
{
  static unsigned long last = 0;
  static bool state = false;

  if (millis() - last >= intervalMs)
  {
    last = millis();
    state = !state;
    digitalWrite(LED_PIN, state);
  }
}

void ledOn() { digitalWrite(LED_PIN, HIGH); }
void ledOff() { digitalWrite(LED_PIN, LOW); }

// ================= WiFi =================
void connectWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED)
  {
    ledBlink(300);
    delay(500);
  }

  Serial.println("WiFi Connected");
}

// ================= MQTT Callback =================
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  StaticJsonDocument<256> doc;
  deserializeJson(doc, payload, length);

  if (doc["request_code"] == RES_NODE_ID)
  {
    Serial.println("Node ID received");
  }
}

// ================= MQTT =================
void connectMQTT()
{
  espClientSecure.setCACert(ISRG_ROOT_X1);
  mqttClient.setServer(MQTT_BROKER_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  while (!mqttClient.connected())
  {
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS))
    {
      mqttClient.subscribe("be_project/test/in");
      ledOn();
    }
    else
    {
      delay(2000);
    }
  }
}

// ================= ESP-NOW =================
void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
  sensor_data_t data;
  memcpy(&data, incomingData, sizeof(data));

  if (data.request_code == REQ_SENSOR_DATA)
  {
    xQueueSend(mqttQueue, &data, 0);
  }
}

// ================= Setup =================
void setup()
{
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  ledOff();

  mqttQueue = xQueueCreate(QUEUE_SIZE, sizeof(sensor_data_t));

  connectWiFi();

  if (esp_now_init() != ESP_OK)
  {
    ESP.restart();
  }

  esp_now_register_recv_cb(onDataRecv);

  connectMQTT();
}

// ================= Loop =================
void loop()
{
  if (!mqttClient.connected())
    connectMQTT();

  mqttClient.loop();

  if (uxQueueMessagesWaiting(mqttQueue))
  {
    sensor_data_t d;
    xQueueReceive(mqttQueue, &d, 0);

    StaticJsonDocument<256> doc;
    doc["reading"] = d.reading;

    char payload[128];
    serializeJson(doc, payload);

    mqttClient.publish("be_project/node/data", payload);
  }

  delay(20);
}