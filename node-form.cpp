#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <HX711.h>

#define LED_PIN 2
#define SEND_INTERVAL 1000
#define MASTER_MAC {0x6C, 0xC8, 0x40, 0x34, 0xAF, 0xB0}
#define WIFI_CHANNEL 5

// ===== HX711 PINS =====
#define HX711_DT 4
#define HX711_SCK 5

// ===== Request Codes =====
#define REQ_SENSOR_DATA 200
#define REQ_NODE_ID 201
#define RES_NODE_ID 202

// ===== CALIBRATION CONSTANTS (derived from 386-point CSV regression) =====
// Formula: weight_g = CALIB_SCALE * (raw - tare_offset) + CALIB_OFFSET
// R² = 0.99999 across 0-630g range
// Do NOT use set_scale() — we apply this formula directly to raw ADC reads
// so the IQR filter operates on unmodified integer values (better precision).
#define CALIB_SCALE -0.00240585f // g per raw ADC unit
#define CALIB_OFFSET -1373.80f   // g intercept (shifts after tare subtraction)

// ===== FILTER SETTINGS =====
#define FILTER_WINDOW 10        // rolling buffer size
#define IQR_MULTIPLIER 1.5f     // standard Tukey fence
#define MIN_DELTA_G 1.0f        // suppress ESP-NOW send if weight change < 1g
#define MAX_PLAUSIBLE_G 5000.0f // hard sanity cap — reject above this

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

HX711 scale;
long tare_offset = 0; // raw ADC value at zero load, captured during setup()

// ===== FILTER STATE =====
long raw_buffer[FILTER_WINDOW];
uint8_t buf_index = 0;
bool buf_full = false;
float last_sent_g = -9999.0f; // forces first reading to always transmit

// ===== HELPER: insertion sort on a copy (non-destructive) =====
void sortCopy(long *src, long *dst, uint8_t n)
{
    memcpy(dst, src, n * sizeof(long));
    for (uint8_t i = 1; i < n; i++)
    {
        long key = dst[i];
        int8_t j = i - 1;
        while (j >= 0 && dst[j] > key)
        {
            dst[j + 1] = dst[j];
            j--;
        }
        dst[j + 1] = key;
    }
}

// ===== IQR MEDIAN FILTER =====
// Returns calibrated weight in grams, or -1.0 if buffer not ready / sanity fail.
float filteredWeight()
{
    uint8_t n = buf_full ? FILTER_WINDOW : buf_index;
    if (n < 4)
        return -1.0f; // need at least 4 points for meaningful IQR

    long sorted[FILTER_WINDOW];
    sortCopy(raw_buffer, sorted, n);

    // Q1 and Q3 by lower/upper quartile index
    long Q1 = sorted[n / 4];
    long Q3 = sorted[(3 * n) / 4];
    long IQR = Q3 - Q1;

    long fence_lo = Q1 - (long)(IQR_MULTIPLIER * IQR);
    long fence_hi = Q3 + (long)(IQR_MULTIPLIER * IQR);

    // Collect inliers
    long inliers[FILTER_WINDOW];
    uint8_t inlier_count = 0;
    for (uint8_t i = 0; i < n; i++)
        if (sorted[i] >= fence_lo && sorted[i] <= fence_hi)
            inliers[inlier_count++] = sorted[i];

    // Fallback: if IQR rejected everything (very noisy burst), use full sorted set
    if (inlier_count == 0)
    {
        Serial.println("⚠ IQR: all readings rejected — using full median fallback");
        inlier_count = n;
        memcpy(inliers, sorted, n * sizeof(long));
    }

    // Median of inliers
    long median_raw = (inlier_count % 2 == 0)
                          ? (inliers[inlier_count / 2 - 1] + inliers[inlier_count / 2]) / 2
                          : inliers[inlier_count / 2];

    // Apply linear calibration (tare-adjusted)
    float weight_g = CALIB_SCALE * (float)(median_raw - tare_offset) + CALIB_OFFSET;

    // Sanity cap
    if (weight_g < -10.0f || weight_g > MAX_PLAUSIBLE_G)
    {
        Serial.printf("⚠ Sanity fail: %.1f g — skipping\n", weight_g);
        return -1.0f;
    }

    Serial.printf("⚖ median_raw=%ld | inliers=%d/%d | weight=%.1fg\n",
                  median_raw, inlier_count, n, weight_g);
    return weight_g;
}

// ===== PUSH ONE RAW READING INTO THE ROLLING BUFFER =====
void pushRawReading()
{
    if (!scale.is_ready())
    {
        Serial.println("⚠ HX711 not ready");
        return;
    }
    raw_buffer[buf_index] = scale.read(); // raw ADC, no scale applied
    buf_index = (buf_index + 1) % FILTER_WINDOW;
    if (buf_index == 0)
        buf_full = true;
}

// ===== HIGH-LEVEL: push + filter =====
float readWeight()
{
    pushRawReading();
    return filteredWeight();
}

// ===== LED =====
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

// ===== ESP-NOW CALLBACKS =====
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

// ===== SEND SENSOR DATA =====
void sendSensorData()
{
    if (nodeId == "")
    {
        Serial.println("⚠️ No Node-ID");
        return;
    }

    float weight_g = readWeight();

    if (weight_g < 0.0f)
    {
        Serial.println("⏳ Buffer filling…");
        return;
    }

    // Suppress if change is below threshold
    if (fabsf(weight_g - last_sent_g) < MIN_DELTA_G)
    {
        Serial.printf("⏸ Delta < %.1fg — suppressed\n", MIN_DELTA_G);
        return;
    }

    sensor_data_t d = {};
    d.request_code = REQ_SENSOR_DATA;
    strncpy(d.node_id, nodeId.c_str(), sizeof(d.node_id));
    strncpy(d.node_mac, WiFi.macAddress().c_str(), sizeof(d.node_mac));

    d.reading = weight_g;
    d.battery_percent = random(60, 100); // TODO: replace with real ADC battery read
    d.timestamp = millis();
    strcpy(d.date_str, "2025-11-11");
    strcpy(d.time_str, "12:00:00");
    d.via = 0;
    strcpy(d.repeater_mac, "00:00:00:00:00:00");
    strcpy(d.master_mac, "6C:C8:40:34:AF:B0");

    esp_now_send(masterMac, (uint8_t *)&d, sizeof(d));
    last_sent_g = weight_g;
    Serial.printf("📤 Sent: %.1f g\n", weight_g);
}

void requestNodeId()
{
    sensor_data_t d = {};
    d.request_code = REQ_NODE_ID;
    strncpy(d.node_mac, WiFi.macAddress().c_str(), sizeof(d.node_mac));
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
    // NOTE: set_scale() is intentionally NOT called.
    // We use scale.read() for raw ADC values and apply CALIB_SCALE manually,
    // preserving full integer precision for the IQR filter.
    delay(2000);

    // Capture raw tare offset (average of 20 readings at zero load)
    tare_offset = scale.read_average(20);
    Serial.printf("✅ Tare offset: %ld\n", tare_offset);

    // Pre-fill the filter buffer before the first send cycle
    Serial.println("⏳ Pre-filling filter buffer...");
    for (uint8_t i = 0; i < FILTER_WINDOW; i++)
    {
        pushRawReading();
        delay(100);
    }
    Serial.println("✅ Load Cell ready — IQR median filter active");

    if (nodeId == "")
    {
        Serial.println("⚠️ No Node-ID → requesting...");
        requestNodeId();
    }
    else
        Serial.printf("✅ Node-ID: %s\n", nodeId.c_str());
}

void loop()
{
    static unsigned long lastReq = 0;

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