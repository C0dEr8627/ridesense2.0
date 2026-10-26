#include <ESP8266WiFi.h>

// Replace these with your phone's hotspot details
const char* WIFI_SSID = "Samsung F15";
const char* WIFI_PASSWORD = "aaliya2303";

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("RideSense 2.0 ESP-12E starting...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to hotspot");

  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startTime < 20000) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WIFI_CONNECTED");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("WIFI_FAILED");
    Serial.println("Check hotspot name and password.");
  }

  Serial.println("ESP test complete.");
}

void loop() {
  // Check Wi-Fi connection periodically
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected. Reconnecting...");
    WiFi.reconnect();
    delay(5000);
  } else {
    delay(1000);
  }
}
