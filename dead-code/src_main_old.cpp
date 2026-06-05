// node_code_2026-04-05.cpp
// FluidCare sensor node — ESP32 + HX711 load cell
//
// Features added vs legacy node_code.cpp:
//   • Linear calibration derived from 7-point least-squares CSV fit
//   • Rolling buffer of 10 raw readings
//   • IQR-based outlier rejection + median for clean weight estimate
//   • Transmit only when cleaned reading differs from last transmitted by >1 g
//   • Light-sleep between readings (saves ~10–20 mA vs bare delay)
//   • Button reset flow: short press → REQ_NODE_ID (201),
//                        long press → REQ_RESET (203)

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <HX711.h>
#include <algorithm>
#include <cstring>

// ====== Pin definitions ======
#define HX711_DOUT_PIN 4
#define HX711_SCK_PIN 5
#define BUTTON_PIN 0 // BOOT button — active LOW
#define LED_PIN 2

// ====== Calibration (7-point least-squares, see reading.csv) ======
// weight_g = CALIB_M * raw_adc + CALIB_B
#define CALIB_M 0.00119454f // g per ADC count
#define CALIB_B 682.7990f   // offset in grams

// ====== IQR / rolling buffer ======
#define ROLLING_BUF_SIZE 10
#define TRANSMIT_DELTA_G 1.0f // only transmit if |delta| > 1 g

// ====== Timing ======
#define SEND_INTERVAL_MS 5000 // minimum ms between transmissions
#define IDLE_SLEEP_MS 80      // light-sleep duration per loop tick
#define LONG_PRESS_MS 2000    // ms to distinguish long vs short press

// ====== Request codes (must match master + backend) ======
#define REQ_SENSOR_DATA 200
#define REQ_NODE_ID 201
#define RES_NODE_ID 202
#define REQ_RESET 203
#define RES_RESET_ACK 204

// ====== Shared packet structure ======
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

// ====== Master MAC (replace with actual master MAC) ======
uint8_t masterMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ====== Global state ======
HX711 scale;
char nodeId[37] = "";  // assigned by backend via 202
char nodeMac[18] = ""; // this device's MAC
bool nodeIdAssigned = false;

int32_t rawBuf[ROLLING_BUF_SIZE];
int bufIndex = 0;
int bufCount = 0;

float lastTransmittedWeight = -9999.0f;
unsigned long lastSendMs = 0;

// ===============================================================
// Calibration: raw ADC → grams
// ===============================================================
inline float rawToGrams(int32_t raw)
{
  return CALIB_M * (float)raw + CALIB_B;
}

// ===============================================================
// IQR-filtered median over the rolling buffer
// Returns NAN if buffer has fewer than 3 samples.
// ===============================================================
float iqrMedianWeight()
{
  if (bufCount < 3)
    return NAN;

  // Copy valid entries into a local array
  int32_t tmp[ROLLING_BUF_SIZE];
  memcpy(tmp, rawBuf, sizeof(int32_t) * bufCount);
  int n = bufCount;

  // Sort ascending
  std::sort(tmp, tmp + n);

  // Q1, Q3, IQR
  float q1 = (float)tmp[n / 4];
  float q3 = (float)tmp[(3 * n) / 4];
  float iqr = q3 - q1;
  float lo = q1 - 1.5f * iqr;
  float hi = q3 + 1.5f * iqr;

  // Collect inliers
  int32_t inliers[ROLLING_BUF_SIZE];
  int ic = 0;
  for (int i = 0; i < n; i++)
  {
    if ((float)tmp[i] >= lo && (float)tmp[i] <= hi)
    {
      inliers[ic++] = tmp[i];
    }
  }
  if (ic == 0)
    return rawToGrams(tmp[n / 2]); // fallback: raw median

  // Median of inliers
  int32_t med = inliers[ic / 2];
  return rawToGrams(med);
}

// ===============================================================
// Push one HX711 reading into the circular buffer
// ===============================================================
void sampleLoadCell()
{
  if (!scale.is_ready())
    return;
  int32_t raw = scale.read(); // raw 24-bit signed value, no library scaling
  rawBuf[bufIndex] = raw;
  bufIndex = (bufIndex + 1) % ROLLING_BUF_SIZE;
  if (bufCount < ROLLING_BUF_SIZE)
    bufCount++;
}

// ===============================================================
// Send a sensor-data packet to master (REQ_SENSOR_DATA = 200)
// ===============================================================
void sendSensorData()
{
  float weight = iqrMedianWeight();
  if (isnan(weight))
    return; // not enough samples yet

  // Delta-based suppression
  if (fabsf(weight - lastTransmittedWeight) <= TRANSMIT_DELTA_G)
    return;

  sensor_data_t pkt = {};
  pkt.request_code = REQ_SENSOR_DATA;
  strncpy(pkt.node_id, nodeId, sizeof(pkt.node_id) - 1);
  strncpy(pkt.node_mac, nodeMac, sizeof(pkt.node_mac) - 1);
  pkt.reading = weight;
  pkt.battery_percent = 100.0f; // TODO: real ADC read
  pkt.timestamp = (uint32_t)(millis() / 1000UL);
  strncpy(pkt.date_str, "2026-04-05", sizeof(pkt.date_str) - 1);
  strncpy(pkt.time_str, "00:00:00", sizeof(pkt.time_str) - 1);
  pkt.via = 0;

  esp_err_t r = esp_now_send(masterMAC, (const uint8_t *)&pkt, sizeof(pkt));
  if (r == ESP_OK)
  {
    lastTransmittedWeight = weight;
    lastSendMs = millis();
    Serial.printf("📤 Sent %.2f g\n", weight);
  }
  else
  {
    Serial.printf("❌ ESP-NOW send error: %d\n", r);
  }
}

// ===============================================================
// Request a Node-ID from backend (REQ_NODE_ID = 201)
// ===============================================================
void requestNodeId()
{
  sensor_data_t pkt = {};
  pkt.request_code = REQ_NODE_ID;
  strncpy(pkt.node_mac, nodeMac, sizeof(pkt.node_mac) - 1);
  esp_now_send(masterMAC, (const uint8_t *)&pkt, sizeof(pkt));
  Serial.println("📤 Sent REQ_NODE_ID (201)");
}

// ===============================================================
// Request a reset from backend (REQ_RESET = 203)
// ===============================================================
void requestReset()
{
  sensor_data_t pkt = {};
  pkt.request_code = REQ_RESET;
  strncpy(pkt.node_id, nodeId, sizeof(pkt.node_id) - 1);
  strncpy(pkt.node_mac, nodeMac, sizeof(pkt.node_mac) - 1);
  esp_now_send(masterMAC, (const uint8_t *)&pkt, sizeof(pkt));
  Serial.println("📤 Sent REQ_RESET (203)");
}

// ===============================================================
// ESP-NOW receive callback
// ===============================================================
void onDataRecv(const uint8_t *mac, const uint8_t *data, int len)
{
  sensor_data_t pkt;
  memcpy(&pkt, data, sizeof(pkt));

  // 202: backend assigned a Node-ID
  if (pkt.request_code == RES_NODE_ID)
  {
    strncpy(nodeId, pkt.node_id, sizeof(nodeId) - 1);
    nodeIdAssigned = true;
    Serial.printf("✅ Got Node-ID: %s\n", nodeId);
  }

  // 204: backend confirmed reset
  else if (pkt.request_code == RES_RESET_ACK)
  {
    Serial.println("🔄 Reset ACK received — clearing Node-ID");
    memset(nodeId, 0, sizeof(nodeId));
    nodeIdAssigned = false;
    lastTransmittedWeight = -9999.0f;
  }
}

// ===============================================================
// Button handler: short press → 201, long press → 203
// ===============================================================
void handleButton()
{
  static unsigned long pressedAt = 0;
  static bool wasPressed = false;

  bool pressed = (digitalRead(BUTTON_PIN) == LOW);

  if (pressed && !wasPressed)
  {
    pressedAt = millis();
    wasPressed = true;
  }
  else if (!pressed && wasPressed)
  {
    unsigned long held = millis() - pressedAt;
    wasPressed = false;
    if (held >= LONG_PRESS_MS)
    {
      requestReset();
    }
    else
    {
      requestNodeId();
    }
  }
}

// ===============================================================
// setup()
// ===============================================================
void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== FluidCare Node 2026-04-05 ===");

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW);

  // Derive node MAC string
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // keep ESP-NOW receiver alive during light-sleep
  uint8_t mac[6];
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  sprintf(nodeMac, "%02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.printf("📟 Node MAC: %s\n", nodeMac);

  // ESP-NOW
  if (esp_now_init() != ESP_OK)
  {
    Serial.println("❌ ESP-NOW init failed");
    ESP.restart();
  }
  esp_now_register_recv_cb(onDataRecv);

  // Register master as peer
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, masterMAC, 6);
  peer.channel = 0;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  // HX711
  scale.begin(HX711_DOUT_PIN, HX711_SCK_PIN);
  Serial.println("✅ HX711 initialized");

  // Request Node-ID on boot if not already stored
  if (!nodeIdAssigned)
  {
    requestNodeId();
  }

  Serial.println("✅ Node ready\n");
}

// ===============================================================
// loop()
// ===============================================================
void loop()
{
  handleButton();

  // Sample load cell into rolling buffer every tick
  sampleLoadCell();

  // Transmit once per SEND_INTERVAL if we have a Node-ID
  if (nodeIdAssigned && (millis() - lastSendMs >= SEND_INTERVAL_MS))
  {
    sendSensorData();
  }

  // Light-sleep for IDLE_SLEEP_MS — CPU sleeps, WiFi/ESP-NOW stays up
  esp_sleep_enable_timer_wakeup((uint64_t)IDLE_SLEEP_MS * 1000ULL);
  esp_light_sleep_start();
}
