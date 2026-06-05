// #include <WiFi.h>
// #include <WiFiClientSecure.h>
// #include <PubSubClient.h>

// const char *WIFI_SSID = "Airtel_sahi_2825";
// const char *WIFI_PASSWORD = "Air@68881";

// const char *MQTT_BROKER_HOST = "1e578bacd37e4198a99e7a4a28756c6e.s1.eu.hivemq.cloud";
// const uint16_t MQTT_PORT = 8883;
// const char *MQTT_CLIENT_ID = "esp32-client-1";
// const char *MQTT_USER = "kanbs";
// const char *MQTT_PASS = "Kartik@3165";

// const int LED_PIN = 2;

// static const char ISRG_ROOT_X1[] PROGMEM = R"EOF(
// -----BEGIN CERTIFICATE-----
// MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
// TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
// cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
// WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
// ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
// MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
// h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
// 0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
// A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
// T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
// B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
// B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
// KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
// OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
// jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
// qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
// rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
// HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbTANBgkq
// hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
// ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
// 3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
// NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
// ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
// TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
// jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
// oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
// 4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
// mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
// emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
// -----END CERTIFICATE-----
// )EOF";

// // ---------- GLOBALS ----------
// WiFiClientSecure espClientSecure;
// PubSubClient mqttClient(espClientSecure);

// // ---------- LED HELPERS ----------
// void ledBlink(unsigned long intervalMs)
// {
//   static unsigned long lastToggle = 0;
//   static bool state = false;
//   unsigned long now = millis();
//   if (now - lastToggle >= intervalMs)
//   {
//     lastToggle = now;
//     state = !state;
//     digitalWrite(LED_PIN, state ? HIGH : LOW);
//   }
// }
// void ledOn() { digitalWrite(LED_PIN, HIGH); }
// void ledOff() { digitalWrite(LED_PIN, LOW); }

// // ---------- WIFI CONNECT ----------
// void connectWiFi()
// {
//   Serial.print("Connecting to WiFi: ");
//   Serial.println(WIFI_SSID);

//   WiFi.disconnect(true);
//   WiFi.mode(WIFI_STA);
//   WiFi.setSleep(false);

//   IPAddress local_IP(192, 168, 1, 55);
//   IPAddress gateway(192, 168, 1, 1);
//   IPAddress subnet(255, 255, 255, 0);
//   IPAddress dns1(1, 1, 1, 1);
//   IPAddress dns2(8, 8, 8, 8);
//   WiFi.config(local_IP, gateway, subnet, dns1, dns2);
//   WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

//   int retry = 0;
//   while (WiFi.status() != WL_CONNECTED)
//   {
//     ledBlink(300);
//     delay(500);
//     if (++retry > 40)
//     {
//       Serial.println("❌ WiFi connect failed, restarting...");
//       ESP.restart();
//     }
//   }

//   Serial.println("\n✅ WiFi Connected (Static Config):");
//   Serial.print("IP Address: ");
//   Serial.println(WiFi.localIP());
//   Serial.print("Gateway: ");
//   Serial.println(WiFi.gatewayIP());
//   Serial.print("DNS: ");
//   Serial.println(WiFi.dnsIP());
//   Serial.println("------------------------");
// }

// // ---------- MQTT CALLBACK ----------
// void mqttCallback(char *topic, byte *payload, unsigned int length)
// {
//   Serial.print("📩 [");
//   Serial.print(topic);
//   Serial.print("] ");
//   for (unsigned int i = 0; i < length; i++)
//     Serial.print((char)payload[i]);
//   Serial.println();
//   ledOff();
//   delay(100);
//   ledOn();
// }

// // ---------- MQTT CONNECT ----------
// void connectMQTT()
// {
//   // Set the root CA certificate
//   espClientSecure.setCACert(ISRG_ROOT_X1);

//   Serial.println("✅ Loaded ISRG Root X1 CA certificate");
//   Serial.println("🔐 Connecting to HiveMQ Cloud with TLS...");

//   // Use hostname instead of IP for proper SNI
//   mqttClient.setServer(MQTT_BROKER_HOST, MQTT_PORT);
//   mqttClient.setCallback(mqttCallback);

//   Serial.print("🔗 Connecting to: ");
//   Serial.println(MQTT_BROKER_HOST);

//   if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS))
//   {
//     Serial.println("✅ Secure MQTT connected to HiveMQ Cloud!");
//     ledOn();
//     mqttClient.subscribe("be_project/test/in");
//     mqttClient.publish("be_project/test/out", "ESP32 securely connected ✅");
//   }
//   else
//   {
//     Serial.print("❌ Connect failed, rc=");
//     Serial.println(mqttClient.state());
//     Serial.println("Error codes: -4=timeout, -3=conn lost, -2=failed, -1=disconnected");
//     delay(5000);
//     ESP.restart();
//   }
// }

// // ---------- SETUP ----------
// void setup()
// {
//   pinMode(LED_PIN, OUTPUT);
//   ledOff();
//   Serial.begin(115200);
//   delay(500);
//   Serial.println("\n🚀 Booting ESP32 with TLS...");
//   connectWiFi();
//   connectMQTT();
// }

// // ---------- LOOP ----------
// void loop()
// {
//   if (WiFi.status() != WL_CONNECTED)
//   {
//     Serial.println("⚠️ WiFi lost → reconnecting...");
//     ledOff();
//     connectWiFi();
//     connectMQTT();
//   }
//   if (!mqttClient.connected())
//   {
//     Serial.println("⚠️ MQTT lost → reconnecting...");
//     ledOff();
//     connectMQTT();
//   }

//   mqttClient.loop();

//   static unsigned long lastMsg = 0;
//   unsigned long now = millis();
//   if (now - lastMsg > 5000)
//   {
//     lastMsg = now;
//     String payload = "Secure heartbeat ✅ Uptime(ms): " + String(now);
//     mqttClient.publish("be_project/test/out", payload.c_str());
//     Serial.println("📤 Published: " + payload);
//   }
// }