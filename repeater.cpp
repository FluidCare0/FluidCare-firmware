// FluidCare repeater — ESP32 (ESP-NOW relay, no WiFi/MQTT)
// Sits between out-of-range nodes and the master.
//
// Uplink  (node → repeater → master):
//   Repeater receives any packet from a node, stamps via=1 and repeater_mac,
//   then forwards to master.
//
// Downlink (master → repeater → node):
//   Master sends packet to repeater MAC with node_mac field set to target node.
//   Repeater reads node_mac, adds node as peer, forwards packet.
//
// Protocol codes (must match master.cpp and node.cpp):
//   200 REQ_NODE_ID
//   201 RES_NODE_ASSIGN
//   202 RES_NODE_CONFIRM
//   203 REQ_SENSOR_DATA
//   204 REQ_TASK_COMPLETE
//   205 RES_ERASE_FLASH
//   206 RES_ERASE_CONFIRM
//   207 REQ_DISCONNECT

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ====== Config — update masterMAC to match master board ======
uint8_t masterMAC[] = {0x6C, 0xC8, 0x40, 0x35, 0x58, 0xC8};
#define WIFI_CHANNEL 6
#define LED_PIN      2

// ====== Shared packet (must match master.cpp and node.cpp) ======
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

// ====== Peer tracking ======
#define MAX_NODE_PEERS 10
uint8_t knownNodes[MAX_NODE_PEERS][6];
int     knownNodeCount = 0;

// Self MAC (filled in setup)
char selfMacStr[18] = "";

// ====== LED ======
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
    static bool          state      = false;
    unsigned long        now        = millis();
    if (now - lastToggle >= 1000UL) {
        lastToggle = now;
        state      = !state;
        digitalWrite(LED_PIN, state ? HIGH : LOW);
    }
}

// ====== Peer helpers ======
static bool macEqual(const uint8_t *a, const uint8_t *b)
{
    return memcmp(a, b, 6) == 0;
}

static bool addPeerIfNeeded(const uint8_t *mac)
{
    if (esp_now_is_peer_exist(mac)) {
        Serial.printf("👥 Peer already known: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return true;
    }

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = WIFI_CHANNEL;
    peer.encrypt = false;

    if (esp_now_add_peer(&peer) == ESP_OK) {
        Serial.printf("✅ Peer added: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return true;
    }
    Serial.printf("❌ Failed to add peer: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return false;
}

static bool parseMacString(const char *macStr, uint8_t *out)
{
    return sscanf(macStr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                  &out[0], &out[1], &out[2], &out[3], &out[4], &out[5]) == 6;
}

static void espnowSendWithRetry(const uint8_t *mac, const sensor_data_t &pkt, const char *dest)
{
    Serial.printf("📤 Forward code=%d → %s (%02X:%02X:%02X:%02X:%02X:%02X)\n",
                  pkt.request_code, dest,
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    for (int i = 0; i < 3; i++) {
        if (esp_now_send(mac, (const uint8_t *)&pkt, sizeof(pkt)) == ESP_OK) {
            Serial.printf("✅ Forwarded code=%d → %s (attempt %d)\n", pkt.request_code, dest, i + 1);
            return;
        }
        Serial.printf("⚠️ Forward failed code=%d → %s (attempt %d/3)\n", pkt.request_code, dest, i + 1);
        delay(100);
    }
    Serial.printf("❌ All 3 attempts failed code=%d → %s (%02X:%02X:%02X:%02X:%02X:%02X)\n",
                  pkt.request_code, dest,
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ledFlash(3, 30, 30);   // 3 rapid — send failed after all retries
}

// ====== ESP-NOW receive ======
void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
    sensor_data_t pkt;
    memcpy(&pkt, incomingData, sizeof(pkt));

    char macStr[18];
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    Serial.printf("📥 recv from=%s code=%d\n", macStr, pkt.request_code);

    if (macEqual(mac, masterMAC)) {
        // ── Downlink: master → node ──────────────────────────────────────
        // master puts target node MAC in pkt.node_mac
        uint8_t nodeMac[6];
        if (!parseMacString(pkt.node_mac, nodeMac)) {
            Serial.printf("❌ Bad node_mac in downlink: '%s'\n", pkt.node_mac);
            return;
        }
        Serial.printf("📡 Downlink code=%d → node %s\n", pkt.request_code, pkt.node_mac);
        addPeerIfNeeded(nodeMac);
        espnowSendWithRetry(nodeMac, pkt, "node");
        ledFlash(2, 50, 50);   // 2 flashes — downlink forwarded
    } else {
        // ── Uplink: node → master ────────────────────────────────────────
        pkt.via = 1;
        strncpy(pkt.repeater_mac, selfMacStr, sizeof(pkt.repeater_mac) - 1);

        // Track this node as a known peer for future downlinks
        bool known = false;
        for (int i = 0; i < knownNodeCount; i++)
            if (macEqual(knownNodes[i], mac)) { known = true; break; }
        if (!known && knownNodeCount < MAX_NODE_PEERS) {
            memcpy(knownNodes[knownNodeCount++], mac, 6);
            Serial.printf("🆕 New node tracked: %s (total=%d/%d)\n", macStr, knownNodeCount, MAX_NODE_PEERS);
            addPeerIfNeeded(mac);   // ensure we can send back to it
        } else if (!known) {
            Serial.printf("⚠️ MAX_NODE_PEERS (%d) reached — cannot track %s\n", MAX_NODE_PEERS, macStr);
        } else {
            Serial.printf("👥 Known node: %s\n", macStr);
        }

        Serial.printf("📡 Uplink code=%d from node %s → master\n", pkt.request_code, macStr);
        espnowSendWithRetry(masterMAC, pkt, "master");
        ledFlash(1, 50, 0);    // 1 flash — uplink forwarded
    }
}

void onDataSent(const uint8_t *mac, esp_now_send_status_t st)
{
    bool toMaster = macEqual(mac, masterMAC);
    Serial.printf("📡 Delivery %s → %s (%02X:%02X:%02X:%02X:%02X:%02X)\n",
                  st == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL",
                  toMaster ? "master" : "node",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ====== setup ======
void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== FluidCare Repeater ===");

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.setSleep(false);

    esp_err_t chErr = esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    uint8_t actualCh; wifi_second_chan_t sch;
    esp_wifi_get_channel(&actualCh, &sch);
    Serial.printf("📡 Channel set=%d err=%d actual=%d\n", WIFI_CHANNEL, chErr, actualCh);

    uint8_t selfMac[6];
    esp_wifi_get_mac(WIFI_IF_STA, selfMac);
    sprintf(selfMacStr, "%02X:%02X:%02X:%02X:%02X:%02X",
            selfMac[0], selfMac[1], selfMac[2], selfMac[3], selfMac[4], selfMac[5]);
    Serial.printf("📟 Repeater MAC: %s\n", selfMacStr);
    Serial.printf("🎯 Master MAC:   %02X:%02X:%02X:%02X:%02X:%02X\n",
                  masterMAC[0], masterMAC[1], masterMAC[2],
                  masterMAC[3], masterMAC[4], masterMAC[5]);

    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ ESP-NOW init failed, restarting...");
        ESP.restart();
    }
    Serial.println("📶 ESP-NOW init: OK");
    esp_now_register_recv_cb(onDataRecv);
    esp_now_register_send_cb(onDataSent);

    // Pre-register master as peer
    if (!addPeerIfNeeded(masterMAC))
        Serial.println("⚠️ Could not pre-add master peer");

    Serial.println("✅ Repeater ready — listening for nodes\n");
}

// ====== loop ======
void loop()
{
    updateStatusLed();
    delay(50);
}
