/*
  RideSense 2.0 - HELMET / BIKE TRANSMITTER
  Target: Arduino UNO
  Branch: feature/esp-telemetry-test

  RF packet format MUST match the receiver:
      helmetStatus,alcoholLevel,sleepStatus
  Example:
      1,0,0

  Connections:
    IR sensor 1 OUT -> A1
    IR sensor 2 OUT -> A2 (assumption: user's "A" means A2)
    MQ-3 DO         -> A0
    433 MHz RF DATA -> D12
    Status LED      -> D7 through a current-limiting resistor
    All grounds     -> common GND

  IMPORTANT:
  - This sketch deliberately does not use a buzzer because D7 is assigned
    to the status LED in the supplied wiring.
  - Confirm each sensor's output polarity and what each IR sensor physically
    detects before relying on these status meanings.
  - IR sensor 1 is mapped to helmetStatus; IR sensor 2 to sleepStatus.
  - Many IR obstacle modules and MQ-3 comparator modules are active LOW.
    Change the *_ACTIVE_LOW constants below if your modules behave oppositely.
*/

#include <RH_ASK.h>
#include <SPI.h>

// ------------------------- Pins -------------------------

const uint8_t IR_HELMET_PIN = A1;
const uint8_t IR_SLEEP_PIN  = A2;  // Assumed from supplied "A" connection
const uint8_t MQ3_DO_PIN    = A0;

const uint8_t RF_TX_PIN     = 12;
const uint8_t RF_RX_UNUSED  = 11;  // RH_ASK constructor requires an RX pin
const uint8_t RF_PTT_UNUSED = 10;  // No PTT hardware connected

const uint8_t STATUS_LED_PIN = 7;

// -------------------- Sensor configuration --------------------

// Set true when the sensor output goes LOW on detection.
const bool IR_HELMET_ACTIVE_LOW = true;
const bool IR_SLEEP_ACTIVE_LOW  = true;
const bool MQ3_ALCOHOL_ACTIVE_LOW = true;

// Keep in sync with the receiver's RH_ASK bitrate (2000).
RH_ASK rf_driver(2000, RF_RX_UNUSED, RF_TX_PIN, RF_PTT_UNUSED);

unsigned long lastTransmitMs = 0;
const unsigned long TRANSMIT_INTERVAL_MS = 250;

bool readDetected(uint8_t pin, bool activeLow)
{
  const int value = digitalRead(pin);
  return activeLow ? (value == LOW) : (value == HIGH);
}

void setup()
{
  pinMode(IR_HELMET_PIN, INPUT);
  pinMode(IR_SLEEP_PIN, INPUT);
  pinMode(MQ3_DO_PIN, INPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);

  digitalWrite(STATUS_LED_PIN, LOW);

  Serial.begin(115200);
  Serial.println(F("RideSense 2.0 Transmitter starting..."));

  if (!rf_driver.init())
  {
    Serial.println(F("RF initialization FAILED"));
    while (true)
    {
      digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
      delay(250);
    }
  }

  Serial.println(F("RF initialization OK"));
  Serial.println(F("Packet: helmet,alcohol,sleep"));
}

void loop()
{
  const bool helmetDetected =
      readDetected(IR_HELMET_PIN, IR_HELMET_ACTIVE_LOW);

  const bool sleepDetected =
      readDetected(IR_SLEEP_PIN, IR_SLEEP_ACTIVE_LOW);

  const bool alcoholDetected =
      readDetected(MQ3_DO_PIN, MQ3_ALCOHOL_ACTIVE_LOW);

  // Receiver expects: helmetStatus,alcoholLevel,sleepStatus
  const char packet[] = {
    helmetDetected ? '1' : '0',
    ',',
    alcoholDetected ? '1' : '0',
    ',',
    sleepDetected ? '1' : '0',
    '\\0'
  };

  // LED indicates the transmitter is operating and sending packets.
  digitalWrite(STATUS_LED_PIN, HIGH);

  if (millis() - lastTransmitMs >= TRANSMIT_INTERVAL_MS)
  {
    lastTransmitMs = millis();

    rf_driver.send(
      reinterpret_cast<const uint8_t *>(packet),
      sizeof(packet) - 1
    );
    rf_driver.waitPacketSent();

    Serial.print(F("TX packet: "));
    Serial.println(packet);
  }

  digitalWrite(STATUS_LED_PIN, LOW);
  delay(5);
}
