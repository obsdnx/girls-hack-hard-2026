/*
  ROVER FIRMWARE - Axiometa Genesis Mini
  Screen AX22-0034 ..... port 2
  HC-SR04 .............. TRIG -> port 3 IO0 (GPIO 9), ECHO -> port 3 IO1 (GPIO 16),
                         GND -> G, VCC -> 3V3
  L9110S wheels ........ A1A=GPIO1, A1B=GPIO2, B1A=GPIO3, B1B=GPIO4
                         Motor battery -> driver power pins only, battery GND joined to board GND

  Libraries: "Adafruit GFX Library" and "Adafruit ST7735 and ST7789 Library"
  Board: "Axiometa Genesis Mini"
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

// ====================== SETTINGS ======================
const char* HOTSPOT_SSID = "wlan5312";
const char* HOTSPOT_PASS = "Ksquare2";
const char* AP_NAME = "ROVER";
const char* AP_PASS = "rover1234";

const int DEFAULT_SPEED = 200;
const int RAMP = 12;
const bool SWAP_TURN = false;
const int STOP_MM = 200;
const bool AUTO_DRIVE = false;          // false = laptop controls via vision
const int AUTO_SPEED = 200;
const unsigned long SLEEP_AFTER_MS = 60000;

// ====================== PINS ======================
#define TRIG_PIN P3_IO0
#define ECHO_PIN P3_IO1

#define TFT_CS   P2_IO0
#define TFT_RST  P2_IO1
#define TFT_DC   P2_IO2
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);
uint16_t GLOW;

const int A1A = 1;
const int A1B = 2;
const int B1A = 3;
const int B1B = 4;

// ====================== WHEELS ======================
int targetA = 0, targetB = 0;
int curA = 0, curB = 0;
unsigned long moveUntil = 0;

bool blocked = false;
int distanceMm = -1;

void writeMotorA(int s) {
  if (s >= 0) { analogWrite(A1A, 0);  analogWrite(A1B, s); }
  else        { analogWrite(A1B, 0);  analogWrite(A1A, -s); }
}

void writeMotorB(int s) {
  if (s >= 0) { analogWrite(B1B, 0);  analogWrite(B1A, s); }
  else        { analogWrite(B1A, 0);  analogWrite(B1B, -s); }
}

void drive(const String& cmd, unsigned long ms, int speed) {
  speed = constrain(speed, 0, 255);
  int turn = SWAP_TURN ? -1 : 1;
  if (cmd == "forward")    { targetA =  speed;        targetB =  speed; }
  else if (cmd == "back")  { targetA = -speed;        targetB = -speed; }
  else if (cmd == "left")  { targetA =  speed * turn; targetB = -speed * turn; }
  else if (cmd == "right") { targetA = -speed * turn; targetB =  speed * turn; }
  else                     { targetA = 0; targetB = 0; ms = 0; }
  moveUntil = ms ? millis() + ms : 0;
}

void updateMotors() {
  static unsigned long last = 0;
  if (millis() - last < 10) return;
  last = millis();

  if (moveUntil && millis() > moveUntil) { targetA = 0; targetB = 0; moveUntil = 0; }

  if (blocked && targetA > 0 && targetB > 0) {
    targetA = 0; targetB = 0; curA = 0; curB = 0;
  }

  curA += constrain(targetA - curA, -RAMP, RAMP);
  curB += constrain(targetB - curB, -RAMP, RAMP);
  writeMotorA(curA);
  writeMotorB(curB);
}

// ====================== AUTO DRIVE ======================
void autoDrive() {
  if (blocked) {
    drive("stop", 0, 0);
  } else {
    drive("forward", 300, AUTO_SPEED);
  }
}

// ====================== OBSTACLE SENSOR (HC-SR04) ======================
void updateSensor() {
  static unsigned long last = 0, clearSince = 0;
  static int closeCount = 0;
  if (millis() - last < 80) return;
  last = millis();

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  unsigned long us = pulseIn(ECHO_PIN, HIGH, 15000);
  distanceMm = us ? (int)(us * 0.1715) : -1;

  bool close = (distanceMm > 0 && distanceMm < STOP_MM);
  closeCount = close ? closeCount + 1 : 0;
  if (closeCount >= 2) {
    blocked = true;
    clearSince = 0;
  } else if (blocked && !close) {
    if (clearSince == 0) clearSince = millis();
    else if (millis() - clearSince > 800) blocked = false;
  }
}

// ====================== FACE ======================
enum FaceMode { FACE_IDLE, FACE_HAPPY, FACE_ALERT, FACE_SLEEP };
FaceMode faceMode = FACE_IDLE;
int faceShown = -1;
bool speaking = false;
unsigned long ipUntil = 0;
unsigned long lastActivity = 0;

void drawBrackets() {
  tft.fillRect(5, 5, 12, 2, GLOW);     tft.fillRect(5, 5, 2, 12, GLOW);
  tft.fillRect(143, 5, 12, 2, GLOW);   tft.fillRect(153, 5, 2, 12, GLOW);
  tft.fillRect(5, 73, 12, 2, GLOW);    tft.fillRect(5, 63, 2, 12, GLOW);
  tft.fillRect(143, 73, 12, 2, GLOW);  tft.fillRect(153, 63, 2, 12, GLOW);
}

void clearEyes() {
  tft.fillRect(22, 14, 52, 52, ST77XX_BLACK);
  tft.fillRect(86, 14, 52, 52, ST77XX_BLACK);
}

void eyesOpen(int dx) {
  clearEyes();
  tft.fillRoundRect(48 + dx - 9, 24, 18, 32, 8, GLOW);
  tft.fillRoundRect(112 + dx - 9, 24, 18, 32, 8, GLOW);
}

void eyesBlink() {
  clearEyes();
  tft.fillRoundRect(48 - 9, 39, 18, 3, 1, GLOW);
  tft.fillRoundRect(112 - 9, 39, 18, 3, 1, GLOW);
}

void happyEye(int cx) {
  tft.fillCircle(cx, 44, 18, GLOW);
  tft.fillCircle(cx, 53, 18, ST77XX_BLACK);
}

void eyesHappy() {
  clearEyes();
  happyEye(48);
  happyEye(112);
}

void eyesAlert() {
  clearEyes();
  tft.fillRoundRect(48 - 9, 20, 18, 40, 8, GLOW);
  tft.fillRoundRect(112 - 9, 20, 18, 40, 8, GLOW);
}

void sleepyEye(int cx) {
  tft.fillCircle(cx, 34, 18, GLOW);
  tft.fillCircle(cx, 25, 18, ST77XX_BLACK);
}

void eyesSleepy() {
  clearEyes();
  sleepyEye(48);
  sleepyEye(112);
}

void drawMouth(int h) {
  tft.fillRect(60, 67, 40, 13, ST77XX_BLACK);
  tft.fillRoundRect(70, 73 - h / 2, 20, h, 2, GLOW);
}

void clearMouth() {
  tft.fillRect(60, 67, 40, 13, ST77XX_BLACK);
}

void showText(const char* line1, const String& line2) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(GLOW);
  tft.setTextSize(1);
  tft.setCursor(8, 12);
  tft.print(line1);
  tft.setTextSize(line2.length() <= 11 ? 2 : 1);
  tft.setCursor(8, 34);
  tft.print(line2);
}

void updateFace() {
  static uint8_t step = 0, zStep = 0;
  static unsigned long next = 0, zNext = 0, nextMouth = 0;
  static bool mouthWasOn = false;

  if (ipUntil) return;

  if ((int)faceMode != faceShown) {
    faceShown = (int)faceMode;
    step = 0;
    next = 0;
    zStep = 0;
    zNext = 0;
    tft.fillRect(139, 18, 20, 45, ST77XX_BLACK);
    if (faceMode == FACE_HAPPY) eyesHappy();
    else if (faceMode == FACE_ALERT) eyesAlert();
    else if (faceMode == FACE_SLEEP) eyesSleepy();
  }

  if (faceMode == FACE_IDLE && millis() >= next) {
    switch (step) {
      case 0: eyesOpen(0);  next = millis() + 1500; break;
      case 1: eyesBlink();  next = millis() + 120;  break;
      case 2: eyesOpen(0);  next = millis() + 900;  break;
      case 3: eyesOpen(-8); next = millis() + 600;  break;
      case 4: eyesOpen(8);  next = millis() + 600;  break;
      case 5: eyesOpen(0);  next = millis() + 500;  break;
      case 6: eyesHappy();  next = millis() + 1800; break;
    }
    step = (step + 1) % 7;
  }

  if (faceMode == FACE_SLEEP && millis() >= zNext) {
    tft.fillRect(139, 18, 20, 45, ST77XX_BLACK);
    tft.setTextColor(GLOW);
    if (zStep >= 1) { tft.setTextSize(1); tft.setCursor(142, 48); tft.print("Z"); }
    if (zStep >= 2) { tft.setTextSize(2); tft.setCursor(140, 24); tft.print("Z"); }
    zStep = (zStep + 1) % 4;
    zNext = millis() + 700;
  }

  if (speaking) {
    if (millis() >= nextMouth) {
      static const uint8_t heights[] = {3, 8, 5, 11, 6, 2, 9};
      drawMouth(heights[random(7)]);
      nextMouth = millis() + 90;
    }
    mouthWasOn = true;
  } else if (mouthWasOn) {
    clearMouth();
    mouthWasOn = false;
  }
}

// ====================== WI-FI + COMMANDS ======================
WebServer server(80);

void wake() {
  lastActivity = millis();
  if (faceMode == FACE_SLEEP) faceMode = FACE_IDLE;
}

void handleDrive() {
  wake();
  String cmd = server.arg("cmd");
  unsigned long ms = (unsigned long)server.arg("ms").toInt();
  int speed = server.hasArg("speed") ? server.arg("speed").toInt() : DEFAULT_SPEED;
  if (ms == 0 && cmd != "stop") ms = 400;
  drive(cmd, ms, speed);
  server.send(200, "text/plain", "ok");
}

void handleSpeak() {
  wake();
  speaking = (server.arg("on") == "1");
  server.send(200, "text/plain", "ok");
}

void handleFace() {
  lastActivity = millis();
  String m = server.arg("mood");
  if (m == "happy") faceMode = FACE_HAPPY;
  else if (m == "alert") faceMode = FACE_ALERT;
  else if (m == "sleep") faceMode = FACE_SLEEP;
  else faceMode = FACE_IDLE;
  server.send(200, "text/plain", "ok");
}

void handleStatus() {
  String j = "{\"blocked\":";
  j += blocked ? "1" : "0";
  j += ",\"dist\":";
  j += String(distanceMm);
  j += "}";
  server.send(200, "application/json", j);
}

String connectWifi(String& label) {
  if (strlen(HOTSPOT_SSID) > 0) {
    showText("Joining Wi-Fi...", String(HOTSPOT_SSID));
    WiFi.mode(WIFI_STA);
    WiFi.begin(HOTSPOT_SSID, HOTSPOT_PASS);
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) delay(250);
    if (WiFi.status() == WL_CONNECTED) {
      label = "Rover address:";
      return WiFi.localIP().toString();
    }
    WiFi.disconnect(true);
  }
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_NAME, AP_PASS);
  label = "Join Wi-Fi ROVER, then:";
  return WiFi.softAPIP().toString();
}

// ====================== MAIN ======================
void setup() {
  Serial.begin(115200);

  SPI.begin(SCK, MISO, MOSI, -1);
  tft.initR(INITR_MINI160x80);
  tft.setSPISpeed(10000000);
  tft.setRotation(3);
  GLOW = tft.color565(120, 230, 255);
  tft.fillScreen(ST77XX_BLACK);
  drawBrackets();
  eyesOpen(0);

  pinMode(A1A, OUTPUT);
  pinMode(A1B, OUTPUT);
  pinMode(B1A, OUTPUT);
  pinMode(B1B, OUTPUT);
  writeMotorA(0);
  writeMotorB(0);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  String label;
  String ip = connectWifi(label);
  Serial.print(label);
  Serial.print(" ");
  Serial.println(ip);
  showText(label.c_str(), ip);
  ipUntil = millis() + 8000;

  server.on("/drive", handleDrive);
  server.on("/speak", handleSpeak);
  server.on("/face", handleFace);
  server.on("/status", handleStatus);
  server.begin();

  lastActivity = millis();
}

void loop() {
  server.handleClient();
  updateSensor();

  if (ipUntil && millis() > ipUntil) {
    ipUntil = 0;
    tft.fillScreen(ST77XX_BLACK);
    drawBrackets();
    faceShown = -1;
  }

  static bool wasBlocked = false;
  if (blocked && !wasBlocked) { faceMode = FACE_ALERT; lastActivity = millis(); }
  if (!blocked && wasBlocked) { faceMode = FACE_IDLE;  lastActivity = millis(); }
  wasBlocked = blocked;

  if (faceMode == FACE_IDLE && !speaking && curA == 0 && curB == 0 &&
      millis() - lastActivity > SLEEP_AFTER_MS) {
    faceMode = FACE_SLEEP;
  }

  if (AUTO_DRIVE) autoDrive();
  updateMotors();
  updateFace();
}
