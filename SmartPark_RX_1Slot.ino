// ═══════════════════════════════════════════════════════════
//   SmartPark RX  v1.0  —  Single Slot Gateway
//   ESP32 + LoRa 433MHz + WiFi + Adafruit IO + HiveMQ
//
//   AIO FEEDS PUBLISHED  (every 16s):
//     parking-status   →  "FREE" / "OCCUPIED" / "RESERVED"
//     slot1-distance   →  "165.0" cm
//     rssi             →  "-53" dBm
//     slot1-booking    →  "FREE" or "RESERVED|SP-XXXX|Name|Vehicle|1hr|14:30"
//
//   AIO FEEDS SUBSCRIBED:
//     servo-angle      →  "1"=OPEN  "0"=CLOSED  (dashboard toggle)
//     booking-status   →  booking from driver website
//
//   HiveMQ TOPIC PUBLISHED (instant):
//     parking/slot1/status  →  "free" / "occupied"
// ═══════════════════════════════════════════════════════════

#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ── PIN DEFINITIONS ──────────────────────────────────────────
#define LORA_SS    32
#define LORA_RST    4
#define LORA_DIO0  26

#define SERVO_PIN    14
#define ANGLE_OPEN   90
#define ANGLE_CLOSED  0

#define RGB_RED    25
#define RGB_GREEN  33
#define RGB_BLUE   27

#define OLED_SDA   21
#define OLED_SCL   22
#define OLED_ADDR  0x3C

// ── WiFi ─────────────────────────────────────────────────────
const char* WIFI_SSID = "mukesh pc";
const char* WIFI_PASS = "Muk@2#34";

// ── HiveMQ Cloud (TLS 8883) ──────────────────────────────────
const char* HIVEMQ_HOST = "3800c882c48b47949d040f181e9d6278.s1.eu.hivemq.cloud";
const int   HIVEMQ_PORT = 8883;
const char* HIVEMQ_USER = "esp32user";
const char* HIVEMQ_PASS = "Test1234";
const char* HIVEMQ_TOPIC = "parking/slot1/status";

// ── Adafruit IO (TCP 1883) ────────────────────────────────────
const char* AIO_HOST = "io.adafruit.com";
const int   AIO_PORT = 1883;
const char* AIO_USER = "jay6025";
const char* AIO_KEY  = "aio_ZEYn128zT3WUzvBFcs84XI203Rpa";

// Publish interval — 5 feeds × 1 per 16s = 18.75 pts/min (safe under 30/min)
const uint32_t AIO_INTERVAL = 16000;

// ── OBJECTS ──────────────────────────────────────────────────
Servo             barrier;
WiFiClientSecure  secClient;
PubSubClient      hivemq(secClient);
WiFiClient        aioClient;
PubSubClient      aio(aioClient);
Adafruit_SSD1306  oled(128, 64, &Wire, -1);

// ── AIO FEED PATHS (built in setup) ──────────────────────────
String AIO_PARKING;       // TX: "FREE" / "OCCUPIED" / "RESERVED"
String AIO_S1_DIST;       // TX: distance cm
String AIO_RSSI_FEED;     // TX: RSSI dBm
String AIO_SLOT1_BOOK;    // TX: booking detail text block
String AIO_SERVO;         // RX: servo toggle from dashboard
String AIO_BOOKING;       // RX: driver booking from website

// ── SLOT STATE ────────────────────────────────────────────────
struct SlotState {
  String status   = "----";   // "free" | "occupied" | "reserved"
  float  dist     = 0.0;
  bool   fresh    = false;
  // Reservation fields
  bool   reserved = false;
  String token    = "";
  String bookedBy = "";        // "Name | Vehicle"
  String bookTime = "";
  String duration = "";
} slot;

uint32_t lastAIO    = 0;
uint32_t pktCount   = 0;
int      lastRSSI   = 0;
int      servoAngle = ANGLE_OPEN;
bool     oledOK     = false;

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

// ── OLED — BOOT ──────────────────────────────────────────────
void oledBoot() {
  oled.clearDisplay();
  oled.fillRect(0, 0, 128, 14, SSD1306_WHITE);
  oled.setTextColor(SSD1306_BLACK); oled.setTextSize(1);
  oled.setCursor(18, 3); oled.print("SmartPark  v1.0");
  oled.setTextColor(SSD1306_WHITE); oled.setTextSize(2);
  oled.setCursor(16, 20); oled.print("GATEWAY");
  oled.setTextSize(1);
  oled.setCursor(16, 42); oled.print("AIO + HiveMQ + LoRa");
  oled.drawRoundRect(0, 54, 128, 10, 2, SSD1306_WHITE);
  oled.setCursor(22, 55); oled.print("Single Slot  S1");
  oled.display(); delay(2000);
}

// ── OLED — CONNECTING ────────────────────────────────────────
void oledMsg(const String& line1, const String& line2 = "") {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE); oled.setTextSize(1);
  oled.setCursor(0, 4);  oled.print(line1);
  oled.setCursor(0, 22); oled.print(line2);
  oled.display();
}

// ── OLED — DASHBOARD ─────────────────────────────────────────
void oledDash() {
  oled.clearDisplay();

  // Header
  oled.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  oled.setTextColor(SSD1306_BLACK); oled.setTextSize(1);
  oled.setCursor(2, 2); oled.print("SmartPark GW  Pkt#"); oled.print(pktCount);
  oled.setTextColor(SSD1306_WHITE);

  // Slot status — big text
  oled.setTextSize(2); oled.setCursor(0, 16);
  if      (slot.reserved)             { oled.print("RESERVED"); }
  else if (slot.status == "occupied") { oled.print("OCCUPIED"); }
  else if (slot.status == "free")     { oled.print("FREE"); }
  else                                { oled.print("----"); }

  // Distance
  oled.setTextSize(1); oled.setCursor(0, 36);
  oled.print("Dist: ");
  if (slot.dist > 0) { oled.print(slot.dist, 1); oled.print(" cm"); }
  else                { oled.print("--"); }

  // Booking token (if reserved)
  oled.setCursor(0, 46);
  if (slot.reserved) {
    oled.print("Token: "); oled.print(slot.token);
  } else {
    oled.print("RSSI: "); oled.print(lastRSSI); oled.print(" dBm");
  }

  // Servo + bottom line
  oled.setCursor(0, 56);
  oled.print("Sv:"); oled.print(servoAngle); oled.print((char)247);
  oled.print(servoAngle == ANGLE_OPEN ? " OPEN" : " CLSD");
  oled.setCursor(80, 56);
  oled.print("R:"); oled.print(lastRSSI);

  oled.display();
}

// ── WiFi CONNECT ─────────────────────────────────────────────
void connectWiFi() {
  Serial.print("[WiFi] Connecting");
  if (oledOK) oledMsg("Connecting WiFi...", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
    if (millis() - t0 > 30000) { Serial.println("\n[WiFi] Timeout — restarting"); ESP.restart(); }
  }
  Serial.println("\n[WiFi] Connected! IP: " + WiFi.localIP().toString());
}

// ── HiveMQ CONNECT ───────────────────────────────────────────
void connectHiveMQ() {
  if (oledOK) oledMsg("HiveMQ TLS...");
  while (!hivemq.connected()) {
    Serial.print("[HiveMQ] Connecting...");
    if (hivemq.connect("SmartPark-GW-1Slot", HIVEMQ_USER, HIVEMQ_PASS)) {
      Serial.println(" OK");
    } else {
      Serial.printf(" Failed rc=%d, retry 3s\n", hivemq.state());
      delay(3000);
    }
  }
}

// ── AIO CALLBACK ─────────────────────────────────────────────
//   Fires when:  servo-angle toggle pressed  OR  website books a slot
// ─────────────────────────────────────────────────────────────
void aioCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();

  Serial.println("\n[AIO RX] Topic  : " + String(topic));
  Serial.println("[AIO RX] Message: " + msg);

  String t = String(topic);

  // ── SERVO TOGGLE FROM DASHBOARD ──────────────────────────
  if (t == AIO_SERVO || t == "/" + AIO_SERVO) {
    String up = msg; up.toUpperCase();
    int angle = (up == "1" || up == "ON" || msg == "90") ? ANGLE_OPEN : ANGLE_CLOSED;
    bool opening = (angle == ANGLE_OPEN);

    Serial.println("[Servo] Dashboard command → " + String(angle) + (opening ? "° OPEN" : "° CLOSED"));

    // Blink LED 4× to signal gate is responding to remote command
    // OPEN command → blink GREEN   |   CLOSE command → blink RED
    for (int i = 0; i < 4; i++) {
      if (opening) rgbGreen(); else rgbRed();
      delay(120);
      rgbOff();
      delay(120);
    }

    // Move servo
    barrier.write(angle);
    servoAngle = angle;
    Serial.println("[Servo] → " + String(angle) + (opening ? "° OPEN ✓" : "° CLOSED ✓"));

    // Settle LED — solid colour confirms final state
    if (opening) rgbGreen(); else rgbRed();

    if (oledOK) oledDash();
    return;
  }

  // ── BOOKING FROM WEBSITE ─────────────────────────────────
  //   Format: "Token:SP-XXXX|Slot:1|Name:Arjun|Vehicle:KL07AB1234|Dur:1hr|Pay:UPI+Rs20|Time:14:30"
  if (t == AIO_BOOKING || t == "/" + AIO_BOOKING) {
    Serial.println("[Booking] Parsing reservation...");

    String tokenVal, nameVal, vehicleVal, durVal, timeVal;
    int    slotNum = -1;

    String remain = msg;
    while (remain.length() > 0) {
      int pipe  = remain.indexOf('|');
      String part = (pipe >= 0) ? remain.substring(0, pipe) : remain;
      remain = (pipe >= 0) ? remain.substring(pipe + 1) : "";
      int colon = part.indexOf(':');
      if (colon < 0) { if (pipe < 0) break; continue; }
      String key = part.substring(0, colon);
      String val = part.substring(colon + 1);
      if      (key == "Token")   tokenVal   = val;
      else if (key == "Slot")    slotNum    = val.toInt();
      else if (key == "Name")    nameVal    = val;
      else if (key == "Vehicle") vehicleVal = val;
      else if (key == "Dur")     durVal     = val;
      else if (key == "Time")    timeVal    = val;
      if (pipe < 0) break;
    }

    // Only Slot 1 exists — reject anything else
    if (slotNum != 1) {
      Serial.println("[Booking] Ignored — not Slot 1");
      return;
    }

    // Reject if sensor already sees a car
    if (slot.status == "occupied") {
      Serial.println("[Booking] Slot 1 is OCCUPIED — rejecting");
      aio.publish(AIO_PARKING.c_str(), "OCCUPIED");
      return;
    }

    // Apply reservation
    slot.reserved = true;
    slot.status   = "reserved";
    slot.token    = tokenVal;
    slot.bookedBy = nameVal + " | " + vehicleVal;
    slot.bookTime = timeVal;
    slot.duration = durVal;
    slot.fresh    = true;

    Serial.printf("[Booking] Slot 1 RESERVED → %s  By: %s  Dur: %s\n",
                  tokenVal.c_str(), slot.bookedBy.c_str(), durVal.c_str());

    // Immediately push status so website sees RESERVED
    aio.publish(AIO_PARKING.c_str(), "RESERVED");

    // Update slot1-booking Text block on dashboard
    String bookingBlock = "RESERVED"
                          " | " + tokenVal +
                          " | " + nameVal +
                          " | " + vehicleVal +
                          " | " + durVal +
                          " | " + timeVal;
    aio.publish(AIO_SLOT1_BOOK.c_str(), bookingBlock.c_str());

    Serial.println("[Booking] slot1-booking → " + bookingBlock);

    rgbYellow(); delay(200); rgbGreen();
    if (oledOK) oledDash();
    return;
  }
}

// ── AIO CONNECT ──────────────────────────────────────────────
void connectAIO() {
  if (oledOK) oledMsg("Adafruit IO...");
  int retries = 0;
  while (!aio.connected()) {
    Serial.print("[AIO] Connecting...");
    String cid = "SmartPark1S-" + String(random(0xffff), HEX);
    if (aio.connect(cid.c_str(), AIO_USER, AIO_KEY)) {
      Serial.println(" OK");
      bool ok1 = aio.subscribe(AIO_SERVO.c_str(),   1);
      bool ok2 = aio.subscribe(AIO_BOOKING.c_str(), 1);
      Serial.println("[AIO] servo-angle    subscribe: " + String(ok1 ? "OK" : "FAILED"));
      Serial.println("[AIO] booking-status subscribe: " + String(ok2 ? "OK" : "FAILED"));
      // Push current servo state so dashboard toggle is in sync
      aio.publish(AIO_SERVO.c_str(), servoAngle == ANGLE_OPEN ? "1" : "0");
    } else {
      Serial.printf(" Failed rc=%d\n", aio.state());
      if (++retries > 10) { Serial.println("[AIO] Check AIO_USER/AIO_KEY"); retries = 0; }
      delay(3000);
    }
  }
}

// ── AIO PUBLISH HELPER ───────────────────────────────────────
//   350ms non-blocking gap — keeps MQTT alive between publishes
// ─────────────────────────────────────────────────────────────
bool aioPublish(const String& feed, const String& value) {
  if (!aio.connected()) connectAIO();
  aio.loop(); hivemq.loop();
  bool ok = aio.publish(feed.c_str(), value.c_str());
  Serial.printf("[AIO TX] %-42s : %s  %s\n", feed.c_str(), value.c_str(), ok ? "✓" : "✗");
  uint32_t t = millis();
  while (millis() - t < 350) { aio.loop(); hivemq.loop(); yield(); }
  return ok;
}

// ── SEND ALL AIO FEEDS ───────────────────────────────────────
void sendAIO() {
  Serial.println("\n[AIO] ═══ Publishing ═══");

  // 1. parking-status
  String ps;
  if      (slot.reserved)              ps = "RESERVED";
  else if (slot.status == "occupied")  ps = "OCCUPIED";
  else if (slot.status == "free")      ps = "FREE";
  else                                 ps = "OFFLINE";
  aioPublish(AIO_PARKING, ps);

  // 2. slot1-distance
  aioPublish(AIO_S1_DIST, slot.dist > 0 ? String(slot.dist, 1) : "0");

  // 3. rssi
  aioPublish(AIO_RSSI_FEED, String(lastRSSI));

  // 4. slot1-booking Text block
  String bookVal;
  if (slot.reserved) {
    bookVal = "RESERVED"
              " | " + slot.token +
              " | " + slot.bookedBy +
              " | " + slot.duration +
              " | " + slot.bookTime;
  } else if (slot.status == "occupied") {
    bookVal = "OCCUPIED — no booking";
  } else if (slot.status == "free") {
    bookVal = "FREE — available";
  } else {
    bookVal = "OFFLINE";
  }
  aioPublish(AIO_SLOT1_BOOK, bookVal);

  Serial.println("[AIO] ═══ Done ═══\n");
  slot.fresh = false;
}

// ── PARSE LORA PACKET ────────────────────────────────────────
//   Expected: "S1,free,165.0,90"  or  "S1,occupied,7.2,0"
// ─────────────────────────────────────────────────────────────
void parsePacket(const String& msg, int rssi) {
  lastRSSI = rssi;

  int c1 = msg.indexOf(',');
  int c2 = msg.indexOf(',', c1 + 1);
  if (c1 < 0 || c2 < 0) {
    Serial.println("[Parser] Bad packet: " + msg);
    return;
  }

  String nodeId = msg.substring(0, c1);
  String status = msg.substring(c1 + 1, c2);
  float  dist   = msg.substring(c2 + 1).toFloat();

  if (nodeId != "S1") {
    Serial.println("[Parser] Unknown node: " + nodeId);
    return;
  }

  // If sensor sees a car at a reserved slot → car arrived, clear reservation
  if (status == "occupied" && slot.reserved) {
    Serial.println("[Booking] Car arrived at reserved slot — clearing token: " + slot.token);
    // Publish arrival note to slot1-booking
    String arrived = "ARRIVED | " + slot.token + " | " + slot.bookedBy + " | " + slot.bookTime;
    aio.publish(AIO_SLOT1_BOOK.c_str(), arrived.c_str());
    Serial.println("[Booking] slot1-booking → " + arrived);
    slot.reserved = false;
    slot.token    = "";
    slot.bookedBy = "";
    slot.bookTime = "";
    slot.duration = "";
  }

  slot.status = status;
  slot.dist   = dist;
  slot.fresh  = true;

  // Publish to HiveMQ instantly
  if (!hivemq.connected()) connectHiveMQ();
  hivemq.publish(HIVEMQ_TOPIC, status.c_str());

  Serial.printf("[S1] %-8s | %5.1f cm | RSSI:%d dBm\n",
                status.c_str(), dist, rssi);
}

// ── SETUP ────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200); delay(600);
  Serial.println("\n==========================================");
  Serial.println("  SmartPark RX GATEWAY  v1.0  |  1-Slot");
  Serial.println("  HiveMQ : TLS 8883");
  Serial.println("  AIO    : TCP 1883");
  Serial.println("==========================================\n");

  // Build feed paths
  AIO_PARKING   = String(AIO_USER) + "/feeds/parking-status";
  AIO_S1_DIST   = String(AIO_USER) + "/feeds/slot1-distance";
  AIO_RSSI_FEED = String(AIO_USER) + "/feeds/rssi";
  AIO_SLOT1_BOOK= String(AIO_USER) + "/feeds/slot1-booking";
  AIO_SERVO     = String(AIO_USER) + "/feeds/servo-angle";
  AIO_BOOKING   = String(AIO_USER) + "/feeds/booking-status";

  Serial.println("[AIO] Feeds:");
  Serial.println("  TX → " + AIO_PARKING);
  Serial.println("  TX → " + AIO_S1_DIST);
  Serial.println("  TX → " + AIO_RSSI_FEED);
  Serial.println("  TX → " + AIO_SLOT1_BOOK);
  Serial.println("  RX ← " + AIO_SERVO);
  Serial.println("  RX ← " + AIO_BOOKING + "\n");

  // GPIO
  pinMode(RGB_RED,   OUTPUT);
  pinMode(RGB_GREEN, OUTPUT);
  pinMode(RGB_BLUE,  OUTPUT);
  rgbBlue();

  // OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if (oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    oledOK = true;
    Serial.println("[OLED]  OK");
    oledBoot();
  } else {
    Serial.println("[OLED]  Not found");
  }

  // Servo
  barrier.attach(SERVO_PIN);
  barrier.write(ANGLE_OPEN);
  servoAngle = ANGLE_OPEN;
  Serial.println("[Servo] OK → 90° OPEN");

  // WiFi
  rgbYellow();
  connectWiFi();

  // HiveMQ
  secClient.setInsecure();
  hivemq.setServer(HIVEMQ_HOST, HIVEMQ_PORT);
  hivemq.setKeepAlive(60);
  connectHiveMQ();

  // Adafruit IO
  aio.setServer(AIO_HOST, AIO_PORT);
  aio.setBufferSize(600);
  aio.setKeepAlive(60);
  aio.setCallback(aioCallback);
  connectAIO();

  // LoRa
  SPI.begin(5, 19, 23, 32);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(433E6)) {
    Serial.println("[LoRa]  INIT FAILED!");
    rgbRed();
    if (oledOK) { oled.clearDisplay(); oled.setTextSize(2); oled.setCursor(10,20); oled.print("LoRa FAIL"); oled.display(); }
    while (1) { rgbRed(); delay(300); rgbOff(); delay(300); }
  }
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0xA5);
  LoRa.enableCrc();
  Serial.println("[LoRa]  OK → SF7|BW125|CR4/5|Sync:0xA5");

  rgbGreen();
  Serial.println("\n>>> Gateway ready — listening <<<\n");
  if (oledOK) oledDash();
}

// ── LOOP ─────────────────────────────────────────────────────
void loop() {

  // HiveMQ keepalive
  if (!hivemq.connected()) { rgbYellow(); connectHiveMQ(); rgbGreen(); }
  hivemq.loop();

  // AIO keepalive
  if (!aio.connected()) connectAIO();
  aio.loop();   // ← fires aioCallback for servo + booking

  // LoRa receive
  int pktSize = LoRa.parsePacket();
  if (pktSize) {
    String msg = "";
    while (LoRa.available()) msg += (char)LoRa.read();
    int rssi = LoRa.packetRssi();
    pktCount++;

    rgbBlue();
    Serial.println("\n──────────────────────────────────────");
    Serial.printf("[LoRa RX] Pkt#%lu  \"%s\"  RSSI:%d dBm\n",
                  pktCount, msg.c_str(), rssi);
    parsePacket(msg, rssi);
    rgbGreen();
    if (oledOK) oledDash();
    aio.loop();   // pump immediately after packet
  }

  // Publish to AIO every 16s (only when fresh data)
  if (slot.fresh && (millis() - lastAIO >= AIO_INTERVAL)) {
    sendAIO();
    lastAIO = millis();
  }
}
