/*
  ============================================================
  RideSense 2.0 - FINAL RECEIVER / BIKE UNIT
  Arduino UNO
  ============================================================

  RECEIVER HARDWARE
  -----------------
  RF Receiver : D11
  Relay       : D4
  Buzzer      : D5

  GPS:
  GPS TX -> Arduino D6
  GPS RX -> Arduino D7

  GSM:
  SIM800L TX -> Arduino D8
  SIM800L RX -> Arduino D9

  I2C:
  SDA -> A4
  SCL -> A5

  I2C DEVICES:
  LCD      -> 0x27
  MPU-6050 -> 0x68

  ============================================================
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RH_ASK.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

// ============================================================
// SETTINGS
// ============================================================

// Change to false if your relay is HIGH-trigger.
const bool RELAY_ACTIVE_LOW = true;

// ------------------------------------------------------------
// LCD
// ------------------------------------------------------------

const byte LCD_ADDRESS = 0x27;

// ------------------------------------------------------------
// EMERGENCY NUMBER
// ------------------------------------------------------------

const char EMERGENCY_NUMBER[] = "+91XXXXXXXXXX";

// IMPORTANT:
// Replace the above with the actual emergency number
// before testing SMS.

// ------------------------------------------------------------
// ACCIDENT DETECTION
// ------------------------------------------------------------

// Starting threshold.
// This MUST be calibrated experimentally.
const float IMPACT_THRESHOLD_G = 2.5;

// ============================================================
// PIN DEFINITIONS
// ============================================================

// Relay
const byte RELAY_PIN = 4;

// Buzzer
const byte BUZZER_PIN = 5;

// ------------------------------------------------------------
// GPS
// ------------------------------------------------------------

const byte GPS_RX_PIN = 6;   // GPS TX -> Arduino D6
const byte GPS_TX_PIN = 7;   // Arduino D7 -> GPS RX

// ------------------------------------------------------------
// GSM
// ------------------------------------------------------------

const byte GSM_RX_PIN = 8;   // SIM800L TX -> Arduino D8
const byte GSM_TX_PIN = 9;   // Arduino D9 -> SIM800L RX

// ------------------------------------------------------------
// RF
// ------------------------------------------------------------

const byte RF_RX_PIN = 11;
const byte RF_TX_PIN = 12;   // Reserved by RH_ASK

// ============================================================
// MODULE OBJECTS
// ============================================================

LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);

RH_ASK rf_driver(
  2000,          // RF bitrate
  RF_RX_PIN,     // RX
  RF_TX_PIN,     // TX
  0
);

SoftwareSerial gpsSerial(
  GPS_RX_PIN,
  GPS_TX_PIN
);

SoftwareSerial gsmSerial(
  GSM_RX_PIN,
  GSM_TX_PIN
);

// ============================================================
// MPU-6050
// ============================================================

const byte MPU_ADDR = 0x68;

// ============================================================
// SYSTEM VARIABLES
// ============================================================

byte helmetStatus = 0;
byte alcoholLevel = 0;
byte sleepStatus = 0;

bool engineState = false;

bool accidentDetected = false;
bool smsSent = false;

// ------------------------------------------------------------
// RF timeout
// ------------------------------------------------------------

unsigned long lastRFTime = 0;

const unsigned long RF_TIMEOUT = 5000;

// ============================================================
// GPS VARIABLES
// ============================================================

bool gpsFix = false;

float latitude = 0.0;
float longitude = 0.0;

char gpsLine[96];

byte gpsIndex = 0;

unsigned long lastGPSStatusPrint = 0;

const unsigned long GPS_STATUS_INTERVAL = 3000;

// ============================================================
// LCD VARIABLES
// ============================================================

byte page = 0;
byte lastPage = 255;

unsigned long lastPageChange = 0;

const unsigned long PAGE_INTERVAL = 3000;

// ============================================================
// RELAY
// ============================================================

void setRelay(bool on)
{
  engineState = on;

  if (RELAY_ACTIVE_LOW)
  {
    digitalWrite(
      RELAY_PIN,
      on ? LOW : HIGH
    );
  }
  else
  {
    digitalWrite(
      RELAY_PIN,
      on ? HIGH : LOW
    );
  }
}

// ============================================================
// BUZZER
// ============================================================

void buzzerOn()
{
  tone(BUZZER_PIN, 2000);
}

void buzzerOff()
{
  noTone(BUZZER_PIN);
  digitalWrite(BUZZER_PIN, LOW);
}

// ============================================================
// MPU-6050
// ============================================================

void mpuWrite(byte reg, byte value)
{
  Wire.beginTransmission(MPU_ADDR);

  Wire.write(reg);
  Wire.write(value);

  Wire.endTransmission();
}

// ------------------------------------------------------------
// Read raw acceleration
// ------------------------------------------------------------

bool readMPU(
  int16_t &ax,
  int16_t &ay,
  int16_t &az
)
{
  Wire.beginTransmission(MPU_ADDR);

  Wire.write(0x3B);

  if (Wire.endTransmission(false) != 0)
  {
    return false;
  }

  Wire.requestFrom(
    MPU_ADDR,
    (byte)6
  );

  if (Wire.available() < 6)
  {
    return false;
  }

  ax =
    (int16_t)(
      (Wire.read() << 8) |
      Wire.read()
    );

  ay =
    (int16_t)(
      (Wire.read() << 8) |
      Wire.read()
    );

  az =
    (int16_t)(
      (Wire.read() << 8) |
      Wire.read()
    );

  return true;
}

// ------------------------------------------------------------
// Get total acceleration in G
// ------------------------------------------------------------

float getAccelerationG()
{
  int16_t ax;
  int16_t ay;
  int16_t az;

  if (!readMPU(ax, ay, az))
  {
    return 0.0;
  }

  // MPU configured for +/-4g
  float x = ax / 8192.0;
  float y = ay / 8192.0;
  float z = az / 8192.0;

  return sqrt(
    x * x +
    y * y +
    z * z
  );
}

// ============================================================
// ACCIDENT DETECTION
// ============================================================

void checkAccident()
{
  if (accidentDetected)
  {
    return;
  }

  float g = getAccelerationG();

  Serial.print(F("Acceleration: "));
  Serial.print(g, 2);
  Serial.println(F(" g"));

  if (g >= IMPACT_THRESHOLD_G)
  {
    accidentDetected = true;

    // Immediately disable engine
    setRelay(false);

    // Immediately activate buzzer
    buzzerOn();

    Serial.println();
    Serial.println(F("=============================="));
    Serial.println(F("!!! ACCIDENT DETECTED !!!"));
    Serial.print(F("Impact: "));
    Serial.print(g, 2);
    Serial.println(F(" g"));
    Serial.println(F("=============================="));
    Serial.println();

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print(F("ACCIDENT!"));

    lcd.setCursor(0, 1);
    lcd.print(F("Engine: OFF"));
  }
}

// ============================================================
// GPS
// ============================================================

// Convert NMEA coordinate format:
//
// DDMM.MMMM
//
// to decimal degrees.
float nmeaCoordinate(
  const char *value,
  char direction
)
{
  float raw = atof(value);

  int degrees = (int)(raw / 100.0);

  float minutes =
    raw -
    (degrees * 100.0);

  float result =
    degrees +
    (minutes / 60.0);

  if (
    direction == 'S' ||
    direction == 'W'
  )
  {
    result = -result;
  }

  return result;
}

// ------------------------------------------------------------
// Parse RMC sentence
// ------------------------------------------------------------

void parseGPSLine()
{
  // Accept both:
  //
  // $GPRMC
  // $GNRMC

  if (
    strncmp(gpsLine, "$GPRMC", 6) != 0 &&
    strncmp(gpsLine, "$GNRMC", 6) != 0
  )
  {
    return;
  }

  char *field;

  byte fieldNo = 0;

  char *ptr = gpsLine;

  char status = 0;

  char latText[16] = "";
  char lonText[16] = "";

  char latDir = 0;
  char lonDir = 0;

  while (
    (field =
      strtok(
        fieldNo == 0 ? ptr : NULL,
        ","
      )
    ) != NULL
  )
  {
    // RMC field 2 = status
    //
    // A = valid
    // V = invalid

    if (fieldNo == 2)
    {
      status = field[0];
    }

    // RMC field 3 = latitude
    if (fieldNo == 3)
    {
      strncpy(
        latText,
        field,
        sizeof(latText) - 1
      );

      latText[
        sizeof(latText) - 1
      ] = '\0';
    }

    // RMC field 4 = N/S
    if (fieldNo == 4)
    {
      latDir = field[0];
    }

    // RMC field 5 = longitude
    if (fieldNo == 5)
    {
      strncpy(
        lonText,
        field,
        sizeof(lonText) - 1
      );

      lonText[
        sizeof(lonText) - 1
      ] = '\0';
    }

    // RMC field 6 = E/W
    if (fieldNo == 6)
    {
      lonDir = field[0];

      break;
    }

    fieldNo++;

    if (fieldNo == 1)
    {
      ptr = NULL;
    }
  }

  // ----------------------------------------------------------
  // Valid GPS fix
  // ----------------------------------------------------------

  if (
    status == 'A' &&
    latText[0] != '\0' &&
    lonText[0] != '\0'
  )
  {
    latitude =
      nmeaCoordinate(
        latText,
        latDir
      );

    longitude =
      nmeaCoordinate(
        lonText,
        lonDir
      );

    gpsFix = true;

    Serial.println();
    Serial.println(F("========== GPS FIX =========="));

    Serial.print(F("Latitude : "));
    Serial.println(
      latitude,
      6
    );

    Serial.print(F("Longitude: "));
    Serial.println(
      longitude,
      6
    );

    Serial.print(F("Google Maps: "));
    Serial.print(
      F("https://maps.google.com/?q=")
    );

    Serial.print(
      latitude,
      6
    );

    Serial.print(',');

    Serial.println(
      longitude,
      6
    );

    Serial.println(F("============================="));
    Serial.println();
  }
}

// ------------------------------------------------------------
// Read GPS data
// ------------------------------------------------------------

void readGPS()
{
  gpsSerial.listen();

  while (gpsSerial.available())
  {
    char c =
      gpsSerial.read();

    // End of NMEA sentence
    if (c == '\n')
    {
      gpsLine[gpsIndex] = '\0';

      if (gpsIndex > 0)
      {
        parseGPSLine();
      }

      gpsIndex = 0;
    }

    else if (c != '\r')
    {
      if (
        gpsIndex <
        sizeof(gpsLine) - 1
      )
      {
        gpsLine[gpsIndex++] = c;
      }
      else
      {
        // Prevent buffer overflow
        gpsIndex = 0;
      }
    }
  }
}

// ------------------------------------------------------------
// GPS diagnostic status
// ------------------------------------------------------------

void printGPSStatus()
{
  if (
    millis() -
    lastGPSStatusPrint <
    GPS_STATUS_INTERVAL
  )
  {
    return;
  }

  lastGPSStatusPrint = millis();

  Serial.print(F("GPS Status: "));

  if (gpsFix)
  {
    Serial.println(F("FIX"));

    Serial.print(F("Latitude : "));
    Serial.println(
      latitude,
      6
    );

    Serial.print(F("Longitude: "));
    Serial.println(
      longitude,
      6
    );
  }
  else
  {
    Serial.println(F("NO FIX"));
  }
}

// ============================================================
// GSM
// ============================================================

// Wait for expected GSM response
bool waitForGSM(
  const char *expected,
  unsigned long timeout
)
{
  unsigned long start =
    millis();

  char buffer[64];

  byte index = 0;

  buffer[0] = '\0';

  while (
    millis() - start <
    timeout
  )
  {
    while (gsmSerial.available())
    {
      char c =
        gsmSerial.read();

      if (
        index <
        sizeof(buffer) - 1
      )
      {
        buffer[index++] = c;

        buffer[index] = '\0';
      }
      else
      {
        memmove(
          buffer,
          buffer + 1,
          sizeof(buffer) - 2
        );

        buffer[
          sizeof(buffer) - 2
        ] = c;

        buffer[
          sizeof(buffer) - 1
        ] = '\0';
      }

      if (
        strstr(
          buffer,
          expected
        ) != NULL
      )
      {
        return true;
      }
    }
  }

  return false;
}

// ------------------------------------------------------------
// Send GSM command
// ------------------------------------------------------------

bool gsmCommand(
  const char *command,
  const char *expected,
  unsigned long timeout
)
{
  Serial.print(F("GSM >> "));
  Serial.println(command);

  gsmSerial.println(command);

  return waitForGSM(
    expected,
    timeout
  );
}

// ------------------------------------------------------------
// Send emergency SMS
// ------------------------------------------------------------

bool sendEmergencySMS()
{
  gsmSerial.listen();

  Serial.println();
  Serial.println(F("GSM: Starting SMS..."));

  // ----------------------------------------------------------
  // Test communication
  // ----------------------------------------------------------

  if (
    !gsmCommand(
      "AT",
      "OK",
      3000
    )
  )
  {
    Serial.println(
      F("GSM: No response")
    );

    return false;
  }

  // ----------------------------------------------------------
  // Disable command echo
  // ----------------------------------------------------------

  gsmCommand(
    "ATE0",
    "OK",
    2000
  );

  // ----------------------------------------------------------
  // SMS text mode
  // ----------------------------------------------------------

  if (
    !gsmCommand(
      "AT+CMGF=1",
      "OK",
      3000
    )
  )
  {
    Serial.println(
      F("GSM: CMGF failed")
    );

    return false;
  }

  // ----------------------------------------------------------
  // Phone number
  // ----------------------------------------------------------

  gsmSerial.print(
    F("AT+CMGS=\"")
  );

  gsmSerial.print(
    EMERGENCY_NUMBER
  );

  gsmSerial.println(
    F("\"")
  );

  // Wait for >
  if (
    !waitForGSM(
      ">",
      5000
    )
  )
  {
    Serial.println(
      F("GSM: CMGS failed")
    );

    return false;
  }

  // ----------------------------------------------------------
  // SMS content
  // ----------------------------------------------------------

  gsmSerial.println(
    F("RIDESENSE ACCIDENT ALERT!")
  );

  gsmSerial.println(
    F("Possible accident detected.")
  );

  if (gpsFix)
  {
    gsmSerial.print(
      F("Location: ")
    );

    gsmSerial.print(
      F("https://maps.google.com/?q=")
    );

    gsmSerial.print(
      latitude,
      6
    );

    gsmSerial.print(',');

    gsmSerial.println(
      longitude,
      6
    );
  }
  else
  {
    gsmSerial.println(
      F("GPS location not available.")
    );
  }

  // ----------------------------------------------------------
  // CTRL+Z
  // ----------------------------------------------------------

  gsmSerial.write(26);

  // Wait for SMS confirmation
  if (
    waitForGSM(
      "OK",
      15000
    )
  )
  {
    Serial.println(
      F("GSM: SMS SENT")
    );

    return true;
  }

  Serial.println(
    F("GSM: SMS FAILED")
  );

  return false;
}

// ============================================================
// RF RECEIVER
// ============================================================

void receiveRF()
{
  uint8_t buffer[20];

  uint8_t length =
    sizeof(buffer) - 1;

  if (
    !rf_driver.recv(
      buffer,
      &length
    )
  )
  {
    return;
  }

  buffer[length] = '\0';

  // ----------------------------------------------------------
  // Find commas
  // ----------------------------------------------------------

  char *firstComma =
    strchr(
      (char *)buffer,
      ','
    );

  if (
    firstComma == NULL
  )
  {
    Serial.println(
      F("RF: Invalid packet")
    );

    return;
  }

  char *secondComma =
    strchr(
      firstComma + 1,
      ','
    );

  if (
    secondComma == NULL
  )
  {
    Serial.println(
      F("RF: Invalid packet")
    );

    return;
  }

  // Split packet
  *firstComma = '\0';
  *secondComma = '\0';

  // ----------------------------------------------------------
  // Extract values
  // ----------------------------------------------------------

  helmetStatus =
    atoi(
      (char *)buffer
    );

  alcoholLevel =
    atoi(
      firstComma + 1
    );

  sleepStatus =
    atoi(
      secondComma + 1
    );

  lastRFTime =
    millis();

  // ----------------------------------------------------------
  // Serial output
  // ----------------------------------------------------------

  Serial.println();
  Serial.println(F("---------- RF DATA ----------"));

  Serial.print(F("Helmet  : "));
  Serial.println(
    helmetStatus
  );

  Serial.print(F("Alcohol : "));
  Serial.println(
    alcoholLevel
  );

  Serial.print(F("Sleep   : "));
  Serial.println(
    sleepStatus
  );

  Serial.println(F("-----------------------------"));

  // ----------------------------------------------------------
  // Accident has highest priority
  // ----------------------------------------------------------

  if (accidentDetected)
  {
    setRelay(false);

    return;
  }

  // ----------------------------------------------------------
  // Engine permission
  // ----------------------------------------------------------

  bool allowEngine =
    (
      helmetStatus == 1 &&
      alcoholLevel == 0
    );

  setRelay(
    allowEngine
  );

  Serial.print(F("Engine : "));

  if (allowEngine)
  {
    Serial.println(F("ON"));
  }
  else
  {
    Serial.println(F("OFF"));
  }
}

// ============================================================
// NORMAL LCD
// ============================================================

void showNormalLCD()
{
  // Change page every 3 seconds
  if (
    millis() -
    lastPageChange >=
    PAGE_INTERVAL
  )
  {
    lastPageChange =
      millis();

    page++;

    if (page > 3)
    {
      page = 0;
    }
  }

  // Don't redraw same page
  if (page == lastPage)
  {
    return;
  }

  lastPage = page;

  lcd.clear();

  // ----------------------------------------------------------
  // PAGE 0
  // ----------------------------------------------------------

  if (page == 0)
  {
    lcd.setCursor(0, 0);

    lcd.print(
      F("Helmet: ")
    );

    lcd.print(
      helmetStatus
        ? F("Yes")
        : F("No ")
    );

    lcd.setCursor(0, 1);

    lcd.print(
      F("Alcohol: ")
    );

    lcd.print(
      alcoholLevel
        ? F("Yes")
        : F("No ")
    );
  }

  // ----------------------------------------------------------
  // PAGE 1
  // ----------------------------------------------------------

  else if (page == 1)
  {
    lcd.setCursor(0, 0);

    lcd.print(
      F("Sleep: ")
    );

    lcd.print(
      sleepStatus
        ? F("Yes")
        : F("No ")
    );

    lcd.setCursor(0, 1);

    lcd.print(
      F("Engine: ")
    );

    lcd.print(
      engineState
        ? F("ON ")
        : F("OFF")
    );
  }

  // ----------------------------------------------------------
  // PAGE 2
  // ----------------------------------------------------------

  else if (page == 2)
  {
    lcd.setCursor(0, 0);

    lcd.print(
      F("GPS: ")
    );

    lcd.print(
      gpsFix
        ? F("FIX   ")
        : F("NO FIX")
    );

    lcd.setCursor(0, 1);

    lcd.print(
      F("Accident: NO")
    );
  }

  // ----------------------------------------------------------
  // PAGE 3
  // ----------------------------------------------------------

  else
  {
    lcd.setCursor(0, 0);

    lcd.print(
      F("RideSense OK")
    );

    lcd.setCursor(0, 1);

    lcd.print(
      F("Monitoring...")
    );
  }
}

// ============================================================
// ACCIDENT LCD
// ============================================================

void showAccidentLCD()
{
  // ----------------------------------------------------------
  // Page 1
  // ----------------------------------------------------------

  lcd.clear();

  lcd.setCursor(0, 0);

  lcd.print(
    F("ACCIDENT!")
  );

  lcd.setCursor(0, 1);

  if (gpsFix)
  {
    lcd.print(
      F("GPS: FIX")
    );
  }
  else
  {
    lcd.print(
      F("GPS: NO FIX")
    );
  }

  delay(2000);

  // ----------------------------------------------------------
  // Page 2
  // ----------------------------------------------------------

  lcd.clear();

  if (gpsFix)
  {
    lcd.setCursor(0, 0);

    lcd.print(
      latitude,
      4
    );

    lcd.setCursor(0, 1);

    lcd.print(
      longitude,
      4
    );
  }
  else
  {
    lcd.setCursor(0, 0);

    lcd.print(
      F("Location")
    );

    lcd.setCursor(0, 1);

    lcd.print(
      F("Unavailable")
    );
  }

  delay(2000);

  // ----------------------------------------------------------
  // Page 3
  // ----------------------------------------------------------

  lcd.clear();

  lcd.setCursor(0, 0);

  lcd.print(
    F("ACCIDENT!")
  );

  lcd.setCursor(0, 1);

  if (smsSent)
  {
    lcd.print(
      F("SMS: SENT")
    );
  }
  else
  {
    lcd.print(
      F("SMS: FAILED")
    );
  }
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
  // ----------------------------------------------------------
  // Serial Monitor
  // ----------------------------------------------------------

  Serial.begin(115200);

  // ----------------------------------------------------------
  // Relay
  // ----------------------------------------------------------

  pinMode(
    RELAY_PIN,
    OUTPUT
  );

  // Engine OFF at startup
  setRelay(false);

  // ----------------------------------------------------------
  // Buzzer
  // ----------------------------------------------------------

  pinMode(
    BUZZER_PIN,
    OUTPUT
  );

  buzzerOff();

  // ----------------------------------------------------------
  // I2C
  // ----------------------------------------------------------

  Wire.begin();

  // ----------------------------------------------------------
  // LCD
  // ----------------------------------------------------------

  lcd.init();

  lcd.backlight();

  lcd.clear();

  lcd.setCursor(0, 0);

  lcd.print(
    F("RideSense")
  );

  lcd.setCursor(0, 1);

  lcd.print(
    F("Starting...")
  );

  delay(1500);

  // ----------------------------------------------------------
  // MPU-6050
  // ----------------------------------------------------------

  mpuWrite(
    0x6B,
    0x00
  );

  // Accelerometer +/-4g
  mpuWrite(
    0x1C,
    0x08
  );

  delay(100);

  // ----------------------------------------------------------
  // GPS
  // ----------------------------------------------------------

  gpsSerial.begin(9600);

  // ----------------------------------------------------------
  // GSM
  // ----------------------------------------------------------

  gsmSerial.begin(9600);

  // ----------------------------------------------------------
  // RF
  // ----------------------------------------------------------

  if (!rf_driver.init())
  {
    Serial.println(
      F("RF initialization FAILED")
    );
  }
  else
  {
    Serial.println(
      F("RF initialization OK")
    );
  }

  // ----------------------------------------------------------
  // Timers
  // ----------------------------------------------------------

  lastRFTime =
    millis();

  lastPageChange =
    millis();

  lastGPSStatusPrint =
    millis();

  // ----------------------------------------------------------
  // Startup display
  // ----------------------------------------------------------

  lcd.clear();

  lcd.setCursor(0, 0);

  lcd.print(
    F("Receiver Ready")
  );

  lcd.setCursor(0, 1);

  lcd.print(
    F("Waiting RF...")
  );

  // ----------------------------------------------------------
  // Serial startup message
  // ----------------------------------------------------------

  Serial.println();
  Serial.println(
    F("==============================")
  );

  Serial.println(
    F("RideSense 2.0 Receiver READY")
  );

  Serial.println(
    F("==============================")
  );

  Serial.println();

  Serial.println(
    F("Pin Configuration:")
  );

  Serial.println(
    F("Relay : D4")
  );

  Serial.println(
    F("Buzzer: D5")
  );

  Serial.println(
    F("GPS   : D6/D7")
  );

  Serial.println(
    F("GSM   : D8/D9")
  );

  Serial.println(
    F("RF    : D11")
  );

  Serial.println(
    F("I2C   : A4/A5")
  );

  Serial.println();
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop()
{
  // ----------------------------------------------------------
  // 1. Receive transmitter data
  // ----------------------------------------------------------

  receiveRF();

  // ----------------------------------------------------------
  // 2. Read GPS continuously
  // ----------------------------------------------------------

  if (!accidentDetected)
  {
    readGPS();

    printGPSStatus();
  }

  // ----------------------------------------------------------
  // 3. RF connection timeout
  // ----------------------------------------------------------

  if (
    millis() -
    lastRFTime >
    RF_TIMEOUT
  )
  {
    // Engine OFF
    setRelay(false);

    // Alert
    buzzerOn();

    lcd.clear();

    lcd.setCursor(0, 0);

    lcd.print(
      F("No Data")
    );

    lcd.setCursor(0, 1);

    lcd.print(
      F("System Offline")
    );

    delay(100);

    return;
  }

  // ----------------------------------------------------------
  // 4. Accident detection
  // ----------------------------------------------------------

  checkAccident();

  // ----------------------------------------------------------
  // 5. ACCIDENT MODE
  // ----------------------------------------------------------

  if (accidentDetected)
  {
    // Engine OFF
    setRelay(false);

    // Buzzer ON
    buzzerOn();

    // --------------------------------------------------------
    // Try to obtain GPS location
    // --------------------------------------------------------

    if (!gpsFix)
    {
      Serial.println(
        F("Accident mode: waiting for GPS...")
      );

      unsigned long gpsStart =
        millis();

      while (
        millis() -
        gpsStart <
        5000
      )
      {
        readGPS();

        delay(10);
      }
    }

    // --------------------------------------------------------
    // Send SMS once
    // --------------------------------------------------------

    if (!smsSent)
    {
      smsSent =
        sendEmergencySMS();
    }

    // --------------------------------------------------------
    // Accident display
    // --------------------------------------------------------

    showAccidentLCD();

    // Keep buzzer ON
    buzzerOn();

    // Stay in accident mode.
    //
    // Press Arduino RESET to restart.
    delay(500);

    return;
  }

  // ----------------------------------------------------------
  // 6. NORMAL MODE
  // ----------------------------------------------------------

  if (
    sleepStatus ||
    alcoholLevel
  )
  {
    buzzerOn();
  }
  else
  {
    buzzerOff();
  }

  // ----------------------------------------------------------
  // LCD
  // ----------------------------------------------------------

  showNormalLCD();

  delay(10);
}