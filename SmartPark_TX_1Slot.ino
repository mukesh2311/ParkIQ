// ═══════════════════════════════════════════════════════════
//   SmartPark TX  v1.0  —  Single Slot Sensor Node
//   ESP32 + HC-SR04 + Servo + LoRa 433MHz + OLED
//
//   Sends every 3s:  "S1,free,165.0,90"
//                    "S1,occupied,7.2,0"
// ═══════════════════════════════════════════════════════════

#include <SPI.h>
#include <LoRa.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ── PIN DEFINITIONS ──────────────────────────────────────────
#define LORA_SS    32
#define LORA_RST    4
#define LORA_DIO0  26

#define TRIG_PIN   16
#define ECHO_PIN   17
#define SERVO_PIN  14

#define RGB_RED    25
#define RGB_GREEN  33
#define RGB_BLUE   27

#define OLED_SDA   21
#define OLED_SCL   22
#define OLED_ADDR  0x3C

// ── CONFIG ───────────────────────────────────────────────────
#define DETECT_CM   10      // distance < 10 cm → OCCUPIED
#define CYCLE_MS  3000      // transmit every 3 seconds
#define ANGLE_OPEN   90     // servo: slot FREE
#define ANGLE_CLOSED  0     // servo: slot OCCUPIED

// ── OBJECTS ──────────────────────────────────────────────────
Servo            barrier;
Adafruit_SSD1306 oled(128, 64, &Wire, -1);

uint32_t packetCount = 0;
String   lastStatus  = "";
int      servoAngle  = ANGLE_OPEN;
bool     oledOK      = false;

// ── RGB ──────────────────────────────────────────────────────
void rgb(bool r, bool g, bool b) {
  digitalWrite(RGB_RED,   r ? HIGH : LOW);
  digitalWrite(RGB_GREEN, g ? HIGH : LOW);
  digitalWrite(RGB_BLUE,  b ? HIGH : LOW);
}
void rgbRed()    { rgb(1,0,0); }
void rgbGreen()  { rgb(0,1,0); }
void rgbBlue()   { rgb(0,0,1); }
void rgbYellow() { rgb(1,1,0); }
void rgbOff()    { rgb(0,0,0); }

// ── HC-SR04 ──────────────────────────────────────────────────
float getDistance() {
  digitalWrite(TRIG_PIN, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long dur = pulseIn(ECHO_PIN, HIGH, 30000);
  if (dur == 0) return -1.0;
  return dur * 0.034f / 2.0f;
}

// ── OLED — BOOT ──────────────────────────────────────────────
void oledBoot() {
  oled.clearDisplay();
  oled.fillRect(0, 0, 128, 14, SSD1306_WHITE);
  oled.setTextColor(SSD1306_BLACK); oled.setTextSize(1);
  oled.setCursor(18, 3); oled.print("SmartPark  v1.0");
  oled.setTextColor(SSD1306_WHITE); oled.setTextSize(2);
  oled.setCursor(10, 20); oled.print("TX SENSOR");
  oled.setTextSize(1);
  oled.setCursor(22, 42); oled.print("Single Slot  S1");
  oled.drawRoundRect(0, 54, 128, 10, 2, SSD1306_WHITE);
  oled.setCursor(8, 55);  oled.print("ESP32 + LoRa 433MHz");
  oled.display(); delay(2000);
}

// ── OLED — RUNTIME ───────────────────────────────────────────
void oledUpdate(float dist, const String& status, bool txOK) {
  oled.clearDisplay();

  // Header bar
  oled.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  oled.setTextColor(SSD1306_BLACK); oled.setTextSize(1);
  oled.setCursor(2, 2);  oled.print("SmartPark TX  S1");
  oled.setCursor(96, 2); oled.print("#"); oled.print(packetCount);
  oled.setTextColor(SSD1306_WHITE);

  // Distance
  oled.setTextSize(1); oled.setCursor(0, 16); oled.print("Dist:");
  oled.setTextSize(2); oled.setCursor(0, 25);
  if (dist < 0) { oled.print("NoEcho"); }
  else { oled.print(dist, 1); oled.print("cm"); }

  // Servo angle
  oled.setTextSize(1);
  oled.setCursor(88, 16); oled.print("Servo");
  oled.setCursor(88, 26); oled.print(servoAngle);
  oled.print((char)247);
  oled.print(servoAngle == ANGLE_OPEN ? "O" : "C");

  // Status banner
  if (status == "occupied") {
    oled.fillRoundRect(0, 44, 128, 14, 3, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK); oled.setCursor(22, 48);
    oled.print(">>> OCCUPIED <<<");
    oled.setTextColor(SSD1306_WHITE);
  } else {
    oled.drawRoundRect(0, 44, 128, 14, 3, SSD1306_WHITE);
    oled.setCursor(36, 48); oled.print("--- FREE ---");
  }

  // LoRa status
  oled.setCursor(0, 59);
  oled.print(txOK ? "LoRa:OK" : "LoRa:ERR");
  oled.display();
}

// ── OLED — ERROR ─────────────────────────────────────────────
void oledError(const String& msg) {
  oled.clearDisplay();
  oled.fillRect(0, 0, 128, 64, SSD1306_WHITE);
  oled.setTextColor(SSD1306_BLACK); oled.setTextSize(2);
  oled.setCursor(26, 4); oled.print("ERROR!");
  oled.drawLine(0, 22, 128, 22, SSD1306_BLACK);
  oled.setTextSize(1); oled.setCursor(4, 28); oled.print(msg);
  oled.setCursor(4, 44); oled.print("Check wiring");
  oled.display();
}

// ── SETUP ────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200); delay(500);
  Serial.println("\n==============================");
  Serial.println("  SmartPark TX v1.0  |  S1");
  Serial.println("==============================\n");

  pinMode(RGB_RED,   OUTPUT);
  pinMode(RGB_GREEN, OUTPUT);
  pinMode(RGB_BLUE,  OUTPUT);
  rgbBlue();

  Wire.begin(OLED_SDA, OLED_SCL);
  if (oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    oledOK = true;
    Serial.println("[OLED]    OK");
    oledBoot();
  } else {
    Serial.println("[OLED]    Not found — continuing");
  }

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  Serial.println("[HC-SR04] OK");

  barrier.attach(SERVO_PIN);
  barrier.write(ANGLE_OPEN);
  servoAngle = ANGLE_OPEN;
  Serial.println("[Servo]   OK → 90° OPEN");

  SPI.begin(5, 19, 23, 32);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(433E6)) {
    Serial.println("[LoRa]    INIT FAILED!");
    rgbRed();
    if (oledOK) oledError("LoRa FAIL");
    while (1) { rgbRed(); delay(300); rgbOff(); delay(300); }
  }
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setTxPower(17);
  LoRa.setSyncWord(0xA5);
  LoRa.enableCrc();
  Serial.println("[LoRa]    OK → SF7|BW125|CR4/5|17dBm|Sync:0xA5");

  rgbGreen();
  Serial.println("\n>>> TX Ready — sending every 3s <<<\n");
}

// ── LOOP ─────────────────────────────────────────────────────
void loop() {
  unsigned long t0 = millis();

  // 1. Measure distance
  float dist = getDistance();
  if (dist < 0) Serial.println("[Sensor]  No echo");
  else          Serial.printf("[Sensor]  %.1f cm\n", dist);

  // 2. Decide status
  String status;
  if (dist > 0 && dist < DETECT_CM) {
    status = "occupied";
  } else {
    status = "free";
  }

  // 3. Act on status CHANGE only (prevents servo jitter every 3s)
  if (status != lastStatus) {
    Serial.printf("[Status]  Changed → %s\n", status.c_str());

    if (status == "occupied") {
      // ── CAR PARKED → close gate ──────────────────────────
      Serial.println("[Gate]    CLOSING → 0°");

      // Blink yellow 3× to warn gate is moving
      for (int i = 0; i < 3; i++) {
        rgbYellow(); delay(150);
        rgbOff();    delay(150);
      }

      servoAngle = ANGLE_CLOSED;
      barrier.write(ANGLE_CLOSED);
      Serial.println("[Servo]   → 0° CLOSED ✓");

      // Solid RED — slot occupied
      rgbRed();

    } else {
      // ── CAR LEFT → open gate ─────────────────────────────
      Serial.println("[Gate]    OPENING → 90°");

      // Blink green 3× to show gate opening
      for (int i = 0; i < 3; i++) {
        rgbGreen(); delay(150);
        rgbOff();   delay(150);
      }

      servoAngle = ANGLE_OPEN;
      barrier.write(ANGLE_OPEN);
      Serial.println("[Servo]   → 90° OPEN ✓");

      // Solid GREEN — slot free
      rgbGreen();
    }

    lastStatus = status;
    if (oledOK) oledUpdate(dist, status, true);
  }

  // 4. Transmit LoRa packet every 3s regardless: "S1,free,165.0,90"
  String pkt = "S1," + status + "," + String(dist, 1) + "," + String(servoAngle);

  rgbBlue();   // blue flash = transmitting
  LoRa.beginPacket();
  LoRa.print(pkt);
  bool sent = LoRa.endPacket();
  packetCount++;

  Serial.printf("[LoRa TX] \"%s\"  Pkt#%lu  %s\n",
                pkt.c_str(), packetCount, sent ? "OK" : "FAIL");

  // Restore LED colour after TX flash
  if (status == "occupied") rgbRed(); else rgbGreen();

  if (oledOK) oledUpdate(dist, status, sent);

  long wait = CYCLE_MS - (long)(millis() - t0);
  if (wait > 0) delay(wait);
}
