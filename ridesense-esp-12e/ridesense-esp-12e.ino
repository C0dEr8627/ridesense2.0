#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>

// Replace these with your phone's hotspot details
const char* WIFI_SSID = "Samsung F15";
const char* WIFI_PASSWORD = "aaliya2303";

// Receive UNO telemetry on GPIO14 (D5 on NodeMCU-style boards).
// GPIO12 (D6) is the unused SoftwareSerial TX pin.
SoftwareSerial arduinoSerial(14, 12);

char telemetryLine[80];
size_t telemetryIndex = 0;

void printStatusLine(const char* line) {
  int helmet = 0;
  int alcohol = 0;
  int sleep = 0;
  int accident = 0;

  // Expected format: STATUS,helmet,alcohol,sleep,accident
  if (sscanf(line, "STATUS,%d,%d,%d,%d",
             &helmet, &alcohol, &sleep, &accident) == 4) {
    Serial.println();
    Serial.println(F("====== RIDESENSE TELEMETRY ======"));
    Serial.print(F("Helmet  : "));
    Serial.println(helmet ? F("YES") : F("NO"));
    Serial.print(F("Alcohol : "));
    Serial.println(alcohol ? F("YES") : F("NO"));
    Serial.print(F("Sleep   : "));
    Serial.println(sleep ? F("YES") : F("NO"));
    Serial.print(F("Accident: "));
    Serial.println(accident ? F("YES") : F("NO"));
    Serial.println(F("================================="));
  } else {
    Serial.print(F("Received (unparsed): "));
    Serial.println(line);
  }
}

void readArduinoTelemetry() {
  while (arduinoSerial.available() > 0) {
    char ch = (char)arduinoSerial.read();

    if (ch == '\r') {
      continue;
    }

    if (ch == '\n') {
      telemetryLine[telemetryIndex] = '\0';
      if (telemetryIndex > 0) {
        printStatusLine(telemetryLine);
      }
      telemetryIndex = 0;
    } else if (telemetryIndex < sizeof(telemetryLine) - 1) {
      telemetryLine[telemetryIndex++] = ch;
    } else {
      // Drop an oversized line and wait for the next newline.
      telemetryIndex = 0;
    }
  }
}

void setup() {
  // Keep USB Serial dedicated to diagnostics; receive UNO data on GPIO14.
  Serial.begin(115200);
  arduinoSerial.begin(9600);
  delay(1000);

  Serial.println();
  Serial.println(F("RideSense 2.0 ESP-12E starting..."));

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print(F("Connecting to hotspot"));

  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startTime < 20000) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("WIFI_CONNECTED"));
    Serial.print(F("IP address: "));
    Serial.println(WiFi.localIP());
    Serial.print(F("Signal strength: "));
    Serial.print(WiFi.RSSI());
    Serial.println(F(" dBm"));
  } else {
    Serial.println(F("WIFI_FAILED"));
    Serial.println(F("Check hotspot name and password."));
  }

  Serial.println(F("Waiting for Arduino telemetry..."));
}

void loop() {
  // Read telemetry regardless of Wi-Fi state; AWS is not required for Phase 1.
  readArduinoTelemetry();

  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastReconnectAttempt = 0;
    if (millis() - lastReconnectAttempt >= 5000) {
      lastReconnectAttempt = millis();
      Serial.println(F("Wi-Fi disconnected. Reconnecting..."));
      WiFi.reconnect();
    }
  }
}
