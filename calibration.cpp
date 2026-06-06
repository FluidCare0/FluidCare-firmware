#include <Arduino.h>
#include <HX711.h>

#define HX711_DOUT_PIN 4
#define HX711_SCK_PIN 5
#define BOOT_BTN_PIN 0

HX711 scale;

void startSession()
{
  Serial.print("\nEnter session name: ");
  while (!Serial.available())
    delay(10);
  String name = Serial.readStringUntil('\n');
  name.trim();
  Serial.printf("# session: %s\n", name.c_str());
  Serial.println("timestamp_ms,raw");
}

void setup()
{
  Serial.begin(115200);
  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
  delay(1000);
  scale.begin(HX711_DOUT_PIN, HX711_SCK_PIN);
  startSession();
}

void loop()
{
  if (digitalRead(BOOT_BTN_PIN) == LOW)
  {
    delay(50);
    if (digitalRead(BOOT_BTN_PIN) == LOW)
    {
      startSession();
      while (digitalRead(BOOT_BTN_PIN) == LOW)
        delay(10);
    }
  }

  while (!scale.is_ready())
    delay(2);
  Serial.printf("%lu,%ld\n", millis(), scale.read());
  delay(50);
}
