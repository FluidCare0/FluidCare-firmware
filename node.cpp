// FluidCare sensor node — ESP32 + HX711 load cell
// Protocol codes (must match master.cpp):
//   200 REQ_NODE_ID       node → master: request an ID assignment
//   201 RES_NODE_ASSIGN   master → node: backend assigned this node_id
//   202 RES_NODE_CONFIRM  node → master: node saved the ID
//   203 REQ_SENSOR_DATA   node → master: weight reading
//   205 RES_ERASE_FLASH   master → node: erase NVS and re-register
//   206 RES_ERASE_CONFIRM node → master: erase done

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <HX711.h>
#include <Preferences.h>
#include <algorithm>

// ====== Pins ======
#define HX711_DOUT_PIN     4
#define HX711_SCK_PIN      5
#define BUTTON_PIN         0    // BOOT button — long press = erase NVS
#define DISCONNECT_BTN_PIN 13   // dedicated button — press = send disconnect (207)
#define LED_PIN            2

// ====== WiFi channel (must match master) ======
#define WIFI_CHANNEL 3

// ====== Calibration (7-point least-squares, see reading.csv) ======
#define CALIB_M 0.00119454f
#define CALIB_B 682.7990f

// ====== Filtering ======
#define ROLLING_BUF_SIZE 10
#define TRANSMIT_DELTA_G 1.0f

// ====== Timing ======
#define SEND_INTERVAL_MS  5000
#define IDLE_SLEEP_MS     80
#define LONG_PRESS_MS     2000
#define NODE_ID_RETRY_MS  10000

// ====== Protocol codes ======
#define REQ_NODE_ID       200
#define RES_NODE_ASSIGN   201
#define RES_NODE_CONFIRM  202
#define REQ_SENSOR_DATA   203
#define RES_ERASE_FLASH   205
#define RES_ERASE_CONFIRM 206
#define REQ_DISCONNECT    207

// ====== Shared packet (must match master) ======
typedef struct sensor_data {
    uint16_t request_code;
    char     node_id[37];
    char     node_mac[18];
    float    reading;
    uint32_t timestamp;
    char     date_str[11];
    char     time_str[9];
    uint8_t  via;
    char     repeater_mac[18];
    char     master_mac[18];
} sensor_data_t;

// ====== Master MAC — update if master board changes ======
uint8_t masterMAC[] = {0x6C, 0xC8, 0x40, 0x35, 0x58, 0xC8};

// ====== State ======
Preferences prefs;
HX711       scale;

char  nodeMac[18] = "";
char  nodeId[37]  = "";
bool  nodeIdAssigned = false;

int32_t rawBuf[ROLLING_BUF_SIZE];
int     bufIndex = 0;
int     bufCount = 0;

float         lastTxWeight = -9999.0f;
unsigned long lastSendMs   = 0;
unsigned long lastIdReqMs  = 0;

// ====== LED ======
// Pattern legend:
//   1× short  (50ms)  — data sent (203)
//   1× medium (150ms) — ID request sent (200)
//   3× fast   (80ms)  — ID assigned / confirmed (201 / 202)
//   5× rapid  (60ms)  — erase NVS (205)
//   3× long   (300ms) — disconnect sent (207)
//   300ms blink loop  — waiting for ID
//   2000ms blink loop — normal idle heartbeat
void ledFlash(int count, int onMs, int gapMs)
{
    for (int i = 0; i < count; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(onMs);
        digitalWrite(LED_PIN, LOW);
        if (i < count - 1) delay(gapMs);
    }
}

void updateStatusLed()
{
    static unsigned long lastToggle = 0;
    static bool          ledState   = false;
    unsigned long        interval   = nodeIdAssigned ? 2000UL : 300UL;
    unsigned long        now        = millis();
    if (now - lastToggle >= interval) {
        lastToggle = now;
        ledState   = !ledState;
        digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    }
}

// ====== Calibration ======
inline float rawToGrams(int32_t raw)
{
    return CALIB_M * (float)raw + CALIB_B;
}

// ====== IQR-filtered median over rolling buffer ======
float iqrMedian()
{
    if (bufCount < 3) return NAN;

    int32_t tmp[ROLLING_BUF_SIZE];
    memcpy(tmp, rawBuf, sizeof(int32_t) * bufCount);
    int n = bufCount;
    std::sort(tmp, tmp + n);

    float q1  = (float)tmp[n / 4];
    float q3  = (float)tmp[(3 * n) / 4];
    float iqr = q3 - q1;
    float lo  = q1 - 1.5f * iqr;
    float hi  = q3 + 1.5f * iqr;

    int32_t inliers[ROLLING_BUF_SIZE];
    int ic = 0;
    for (int i = 0; i < n; i++)
        if ((float)tmp[i] >= lo && (float)tmp[i] <= hi)
            inliers[ic++] = tmp[i];

    return rawToGrams(ic > 0 ? inliers[ic / 2] : tmp[n / 2]);
}

// ====== Load cell ======
void sampleLoadCell()
{
    if (!scale.is_ready()) return;
    int32_t raw = scale.read();
    rawBuf[bufIndex] = raw;
    bufIndex = (bufIndex + 1) % ROLLING_BUF_SIZE;
    if (bufCount < ROLLING_BUF_SIZE) bufCount++;
}

// ====== Send helpers ======
static void sendPacket(sensor_data_t &pkt)
{
    Serial.printf("📡 espnow_send code=%d size=%d to=%02X:%02X:%02X:%02X:%02X:%02X\n",
                  pkt.request_code, sizeof(pkt),
                  masterMAC[0], masterMAC[1], masterMAC[2],
                  masterMAC[3], masterMAC[4], masterMAC[5]);
    esp_err_t r = esp_now_send(masterMAC, (const uint8_t *)&pkt, sizeof(pkt));
    Serial.printf("   send queued: %s (err=%d)\n", r == ESP_OK ? "OK" : "FAIL", r);
}

void requestNodeId()
{
    sensor_data_t pkt = {};
    pkt.request_code = REQ_NODE_ID;
    strncpy(pkt.node_mac, nodeMac, sizeof(pkt.node_mac) - 1);
    sendPacket(pkt);
    lastIdReqMs = millis();
    ledFlash(1, 150, 0);   // 1 medium flash — ID request sent
    Serial.println("📤 REQ_NODE_ID (200)");
}

static void confirmNodeId()
{
    sensor_data_t pkt = {};
    pkt.request_code = RES_NODE_CONFIRM;
    strncpy(pkt.node_id,  nodeId,  sizeof(pkt.node_id)  - 1);
    strncpy(pkt.node_mac, nodeMac, sizeof(pkt.node_mac) - 1);
    sendPacket(pkt);
    ledFlash(3, 80, 80);   // 3 fast flashes — ID confirmed
    Serial.printf("📤 RES_NODE_CONFIRM (202) id=%s\n", nodeId);
}

void sendSensorData()
{
    float weight = iqrMedian();
    if (isnan(weight)) return;
    if (fabsf(weight - lastTxWeight) <= TRANSMIT_DELTA_G) return;

    sensor_data_t pkt = {};
    pkt.request_code = REQ_SENSOR_DATA;
    strncpy(pkt.node_id,  nodeId,  sizeof(pkt.node_id)  - 1);
    strncpy(pkt.node_mac, nodeMac, sizeof(pkt.node_mac) - 1);
    pkt.reading   = weight;
    pkt.timestamp = (uint32_t)(millis() / 1000UL);
    pkt.via = 0;

    sendPacket(pkt);
    lastTxWeight = weight;
    lastSendMs   = millis();
    ledFlash(1, 50, 0);    // 1 short flash — sensor data sent
    Serial.printf("📤 REQ_SENSOR_DATA (203) %.2f g\n", weight);
}

// ====== NVS ======
static void saveNodeId(const char *id)
{
    prefs.begin("node_cfg", false);
    prefs.putString("node_id", id);
    prefs.end();
}

static void eraseNode()
{
    ledFlash(5, 60, 60);   // 5 rapid flashes — erasing NVS
    prefs.begin("node_cfg", false);
    prefs.clear();
    prefs.end();
    memset(nodeId, 0, sizeof(nodeId));
    nodeIdAssigned = false;
    lastTxWeight   = -9999.0f;
    Serial.println("🗑️ NVS cleared");
}

static void sendDisconnect()
{
    sensor_data_t pkt = {};
    pkt.request_code = REQ_DISCONNECT;
    strncpy(pkt.node_id,  nodeId,  sizeof(pkt.node_id)  - 1);
    strncpy(pkt.node_mac, nodeMac, sizeof(pkt.node_mac) - 1);
    sendPacket(pkt);
    ledFlash(3, 300, 100); // 3 long pulses — going offline
    Serial.println("📤 REQ_DISCONNECT (207)");
}

// ====== ESP-NOW receive ======
void onDataRecv(const uint8_t *, const uint8_t *data, int)
{
    sensor_data_t pkt;
    memcpy(&pkt, data, sizeof(pkt));

    Serial.printf("📥 onDataRecv: len=%d code=%d from=%02X:%02X:%02X:%02X:%02X:%02X\n",
                  len, pkt.request_code,
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    switch (pkt.request_code)
    {
    case RES_NODE_ASSIGN:
        Serial.printf("💾 Saving node_id: %s\n", pkt.node_id);
        strncpy(nodeId, pkt.node_id, sizeof(nodeId) - 1);
        nodeIdAssigned = true;
        saveNodeId(nodeId);
        ledFlash(3, 80, 80);   // 3 fast flashes — ID received from master
        Serial.printf("✅ Node-ID assigned: %s\n", nodeId);
        confirmNodeId();
        break;

    case RES_ERASE_FLASH:
    {
        Serial.println("🔴 Erase flash received");
        eraseNode();
        sensor_data_t reply = {};
        reply.request_code = RES_ERASE_CONFIRM;
        strncpy(reply.node_mac, nodeMac, sizeof(reply.node_mac) - 1);
        sendPacket(reply);
        Serial.println("📤 RES_ERASE_CONFIRM (206)");
        break;
    }

    default:
        Serial.printf("⚠️ Unknown code: %d\n", pkt.request_code);
        break;
    }
}

void onDataSent(const uint8_t *, esp_now_send_status_t st)
{
    if (st != ESP_NOW_SEND_SUCCESS)
        ledFlash(2, 50, 50); // 2 quick flashes — send failed
    Serial.printf("📡 Send %s\n", st == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

// ====== Button ======
void handleButton()
{
    static unsigned long pressedAt = 0;
    static bool wasPressed = false;

    bool pressed = (digitalRead(BUTTON_PIN) == LOW);

    if (pressed && !wasPressed) {
        pressedAt  = millis();
        wasPressed = true;
    } else if (!pressed && wasPressed) {
        unsigned long held = millis() - pressedAt;
        wasPressed = false;
        if (held >= LONG_PRESS_MS) {
            eraseNode();
        }
        requestNodeId();
    }
}

// ====== Disconnect button ======
// GPIO 13, INPUT_PULLUP — press ≥500ms sends disconnect (207)
#define DISCONNECT_HOLD_MS 500
void handleDisconnectButton()
{
    static unsigned long pressedAt = 0;
    static bool          wasPressed = false;

    bool pressed = (digitalRead(DISCONNECT_BTN_PIN) == LOW);

    if (pressed && !wasPressed) {
        pressedAt  = millis();
        wasPressed = true;
    } else if (!pressed && wasPressed) {
        unsigned long held = millis() - pressedAt;
        wasPressed = false;
        if (held < DISCONNECT_HOLD_MS) return;
        if (nodeIdAssigned)
            sendDisconnect();
        else
            Serial.println("⚠️ Disconnect ignored — no node ID yet");
    }
}

// ====== Setup ======
void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== FluidCare Node ===");

    pinMode(LED_PIN,            OUTPUT);
    pinMode(BUTTON_PIN,         INPUT_PULLUP);
    pinMode(DISCONNECT_BTN_PIN, INPUT_PULLUP);
    digitalWrite(LED_PIN, LOW);

    prefs.begin("node_cfg", false);
    String saved = prefs.getString("node_id", "");
    prefs.end();
    if (saved.length() > 0) {
        strncpy(nodeId, saved.c_str(), sizeof(nodeId) - 1);
        nodeIdAssigned = true;
        Serial.printf("💾 Restored Node-ID: %s\n", nodeId);
    }

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.setSleep(false);

    esp_err_t chErr = esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    uint8_t actualCh; wifi_second_chan_t sch2;
    esp_wifi_get_channel(&actualCh, &sch2);
    Serial.printf("📡 Channel set=%d err=%d actual=%d\n", WIFI_CHANNEL, chErr, actualCh);

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    sprintf(nodeMac, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.printf("📟 Node MAC: %s\n", nodeMac);
    Serial.printf("🎯 Target master MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  masterMAC[0], masterMAC[1], masterMAC[2],
                  masterMAC[3], masterMAC[4], masterMAC[5]);

    esp_err_t nowErr = esp_now_init();
    Serial.printf("📶 ESP-NOW init: %s (err=%d)\n", nowErr == ESP_OK ? "OK" : "FAIL", nowErr);
    if (nowErr != ESP_OK) ESP.restart();
    esp_now_register_recv_cb(onDataRecv);
    esp_now_register_send_cb(onDataSent);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, masterMAC, 6);
    peer.channel = WIFI_CHANNEL;
    peer.encrypt = false;
    esp_err_t peerErr = esp_now_add_peer(&peer);
    Serial.printf("👥 Add peer: %s (err=%d)\n", peerErr == ESP_OK ? "OK" : "FAIL", peerErr);

    scale.begin(HX711_DOUT_PIN, HX711_SCK_PIN);
    Serial.println("✅ HX711 initialized");

    if (!nodeIdAssigned)
        requestNodeId();

    Serial.println("✅ Node ready\n");
}

// ====== Loop ======
void loop()
{
    handleButton();
    handleDisconnectButton();
    sampleLoadCell();
    updateStatusLed();

    if (!nodeIdAssigned && millis() - lastIdReqMs >= NODE_ID_RETRY_MS)
        requestNodeId();

    if (nodeIdAssigned && millis() - lastSendMs >= SEND_INTERVAL_MS)
        sendSensorData();

    delay(IDLE_SLEEP_MS);
}
