/*
  ROVER FIRMWARE - Axiometa Genesis Mini
  =====================================================================
  What it does
    - Joins your phone hotspot (or makes its own "ROVER" Wi-Fi if it can't) and shows its
      address on the screen for 8 seconds at power-up
    - Drives the L9110S wheels when the laptop tells it to (smooth start and stop)
    - Animated face on the AX22-0034 screen (port 2):
        idle (blinks, looks around), happy, alert (obstacle), sleepy with Zs, talking mouth
    - HC-SR04 ultrasonic sensor (port 3): the rover stops by itself if something is ahead
    - Falls asleep after 60 s with nothing to do, wakes when a command arrives or someone steps close

  Setup
    1. Boards Manager: "esp32" by Espressif Systems (3.3.0 or newer). Board: "Axiometa Genesis Mini".
    2. Library Manager: "Adafruit GFX Library" and "Adafruit ST7735 and ST7789 Library".
    3. Type your phone hotspot name and password below (iPhone: turn ON "Maximise Compatibility",
       the rover only does 2.4 GHz Wi-Fi). Leave HOTSPOT_SSID as "" to use the rover's own Wi-Fi.
    4. Upload. The screen shows the rover's address - type it into the laptop script.

  Wiring
    Screen AX22-0034 ..... port 2 (plug in)
    HC-SR04 .............. TRIG -> port 3 IO0, ECHO -> port 3 IO1, GND -> GND,
                           VCC -> 3V3 (try this first, then no resistors are needed).
                           If readings are flaky use 5 V on VCC AND a divider on ECHO
                           (ECHO -> 1k -> pin, pin -> 2k -> GND). Never connect a 5 V ECHO straight to the board.
    L9110S wheels ........ A1A=GPIO1, A1B=GPIO2, B1A=GPIO3, B1B=GPIO4 (as in your wheels test)
                           Motor battery -> driver power pins only. Battery GND joined to board GND.

    Wheel encoders (OPTIONAL, makes the path much straighter): the two speed-sensor modules
                           OUT -> GPIO 17 (left/A wheel) and GPIO 18 (right/B wheel), VCC -> 3V3, GND -> GND.
                           Then set ENCODERS = true below.

  Commands the laptop sends (HTTP GET):
    /drive?cmd=forward|back|left|right|stop&ms=400&speed=200      (old style, stops by itself)
    /move?cm=100        drive forward 100 cm (negative = backwards). The rover measures it itself.
    /turn?deg=90        turn on the spot 90 degrees (positive = left, negative = right)
    /calib?cm=..&deg=..&bias=..   tune the numbers without re-uploading (the laptop does this for you)
    /speak?on=1  or  /speak?on=0
    /face?mood=idle|happy|alert|sleep
    /status   ->  {"blocked":0,"dist":-1,"busy":0,"res":0,"enc":0,"ta":0,"tb":0,"cm":15.0,"deg":120.0,"bias":0.0}
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

// ====================== SETTINGS ======================
const char* HOTSPOT_SSID = "";          // <- your phone hotspot name ("" = use the rover's own Wi-Fi)
const char* HOTSPOT_PASS = "";          // <- its password
const char* AP_NAME = "ROVER";          // the rover's own Wi-Fi (fallback)
const char* AP_PASS = "rover1234";

const int DEFAULT_SPEED = 120;          // 0-255. Slow for testing. If the wheels hum but don't move, raise it (try 150-180).
const bool FLIP_WHEELS = true;          // true = swaps forward and backward (set false if it ever drives the wrong way round)
const float WHEEL_A_SCALE = 1.0;        // fix drifting: 1.0 = full power. Lower one wheel (e.g. 0.85) to slow it down.
const float WHEEL_B_SCALE = 1.0;        // if the rover curves to one side, slow the faster wheel a little
const int RAMP = 12;                    // how fast speed changes every 10 ms (smooth start/stop)
const bool SWAP_TURN = false;           // set true if "left" turns the rover right
const int STOP_MM = 200;                // something closer than this = stop (millimetres)
const bool AUTO_DRIVE = false;          // false = only moves when the laptop tells it to (use this for the QR demo)
                                        // true = rover drives forward by itself and stops for obstacles (testing)
const int AUTO_SPEED = 120;             // speed used in AUTO_DRIVE mode
const unsigned long SLEEP_AFTER_MS = 60000;   // fall asleep after this long with nothing happening

// ---- Precise moves (/move and /turn) ----
const int MOVE_SPEED = 150;             // speed used for /move and /turn (a bit faster = steadier than 120)
const bool ENCODERS = false;            // true once the two wheel speed sensors are wired (GPIO 17 and 18)
const int ENC_A_PIN = 17;               // sensor on wheel A
const int ENC_B_PIN = 18;               // sensor on wheel B
const float MM_PER_TICK = 10.2;         // wheel circumference / slots in the disc (65 mm wheel, 20 slots = 10.2)
const float TRACK_MM = 130.0;           // distance between the two wheels (measure yours)
const float STRAIGHT_KP = 8.0;          // how hard it corrects drifting (encoders only)
const float STRAIGHT_KI = 6.0;          // how fast it learns that one wheel is weaker (encoders only)

// ====================== PINS ======================
#define TRIG_PIN P3_IO0                 // ultrasonic sensor in port 3
#define ECHO_PIN P3_IO1

#define TFT_CS   P2_IO0                 // screen in port 2
#define TFT_RST  P2_IO1
#define TFT_DC   P2_IO2
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);
uint16_t GLOW;

const int A1A = 1;                      // wheels (L9110S)
const int A1B = 2;
const int B1A = 3;
const int B1B = 4;

// ====================== WHEELS ======================
int targetA = 0, targetB = 0;           // wanted speed -255..255 (negative = backwards)
int curA = 0, curB = 0;                 // actual speed right now
unsigned long moveUntil = 0;            // wheels stop by themselves at this time (safety)

bool blocked = false;
int distanceMm = -1;

// Numbers the laptop can tune (/calib). Without encoders: cm per second and degrees per second.
// With encoders: cm per tick and degrees per tick.
float calCm  = ENCODERS ? MM_PER_TICK / 10.0 : 15.0;
float calDeg = ENCODERS ? 360.0 * MM_PER_TICK / (PI * TRACK_MM) : 120.0;
float calBias = 0.0;                    // > 0 slows wheel A, < 0 slows wheel B (fixes drifting)

float scaleA() { return WHEEL_A_SCALE * (calBias > 0 ? 1.0 - calBias : 1.0); }
float scaleB() { return WHEEL_B_SCALE * (calBias < 0 ? 1.0 + calBias : 1.0); }

void writeMotorA(int s) {
  s = (int)(s * scaleA());
  if (FLIP_WHEELS) s = -s;
  if (s >= 0) { analogWrite(A1A, 0);  analogWrite(A1B, s); }
  else        { analogWrite(A1B, 0);  analogWrite(A1A, -s); }
}

void writeMotorB(int s) {
  s = (int)(s * scaleB());
  if (FLIP_WHEELS) s = -s;
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

  // Something ahead: hard stop going forward, but turning and reversing still work
  if (blocked && targetA > 0 && targetB > 0) {
    targetA = 0; targetB = 0; curA = 0; curB = 0;
  }

  curA += constrain(targetA - curA, -RAMP, RAMP);
  curB += constrain(targetB - curB, -RAMP, RAMP);
  writeMotorA(curA);
  writeMotorB(curB);
}

// ====================== PRECISE MOVES (/move and /turn) ======================
// The rover measures the move itself. With wheel encoders it counts wheel turns and keeps both
// wheels level (straight line). Without them it uses timing and the calibrated numbers.
volatile long ticksA = 0, ticksB = 0;
void IRAM_ATTR isrA() { static unsigned long l = 0; unsigned long n = micros(); if (n - l > 1500) { ticksA++; l = n; } }
void IRAM_ATTR isrB() { static unsigned long l = 0; unsigned long n = micros(); if (n - l > 1500) { ticksB++; l = n; } }

enum MoveMode { MV_NONE, MV_STRAIGHT, MV_TURN };
MoveMode mvMode = MV_NONE;
int mvDir = 1;                          // +1 forward / left, -1 back / right
float mvGoal = 0;                       // ticks (encoders) or milliseconds (timing)
unsigned long mvRun = 0;                // milliseconds the wheels have really been running
long mvA0 = 0, mvB0 = 0;                // tick counts when the move began
long mvLastSum = 0;
float mvI = 0;                          // remembers the drift so far (encoders only)
unsigned long mvLastProgress = 0;
int mvResult = 0;                       // 0 = none/running, 1 = finished, 2 = stuck, 3 = cancelled

void stopMove(int result) {
  if (mvMode != MV_NONE) mvResult = result;
  mvMode = MV_NONE;
  targetA = 0; targetB = 0; curA = 0; curB = 0;
  writeMotorA(0);
  writeMotorB(0);
}

void startMove(MoveMode mode, int dir, float amount) {
  float goal = amount / (mode == MV_STRAIGHT ? calCm : calDeg);   // ticks, or seconds
  mvGoal = ENCODERS ? goal : goal * 1000.0;
  mvMode = mode;
  mvDir = dir;
  mvRun = 0;
  mvA0 = ticksA;
  mvB0 = ticksB;
  mvLastSum = 0;
  mvI = 0;
  mvLastProgress = 0;
  mvResult = 0;
  moveUntil = 0;
}

void updateMove() {
  static unsigned long last = 0;
  if (mvMode == MV_NONE) { last = millis(); return; }
  unsigned long now = millis();
  if (now - last < 20) return;
  unsigned long dt = now - last;
  last = now;
  if (dt > 100) dt = 100;

  // Something in the way while driving forward: wait. The clock stops, so no distance is lost.
  if (mvMode == MV_STRAIGHT && mvDir > 0 && blocked) {
    targetA = 0; targetB = 0; curA = 0; curB = 0;
    writeMotorA(0);
    writeMotorB(0);
    return;
  }

  mvRun += dt;
  long a = ticksA - mvA0;
  long b = ticksB - mvB0;

  bool done;
  if (ENCODERS) {
    done = ((a + b) / 2.0) >= mvGoal;
    if (a + b != mvLastSum) { mvLastSum = a + b; mvLastProgress = mvRun; }
    else if (mvRun - mvLastProgress > 1500) { stopMove(2); return; }   // wheels not turning
  } else {
    done = (float)mvRun >= mvGoal;
  }
  if (done) { stopMove(1); return; }

  int corr = 0;                         // keep both wheels level (encoders only)
  if (ENCODERS && mvRun > 300) {
    float e = (float)(a - b);           // positive = wheel A is ahead
    mvI = constrain(mvI + e * dt / 1000.0, -10.0f, 10.0f);
    corr = constrain((int)(STRAIGHT_KP * e + STRAIGHT_KI * mvI), -60, 60);
  }
  int sa = constrain(MOVE_SPEED - corr / 2, 0, 255);
  int sb = constrain(MOVE_SPEED + corr / 2, 0, 255);

  int turn = SWAP_TURN ? -1 : 1;
  if (mvMode == MV_STRAIGHT) { targetA = mvDir * sa;        targetB = mvDir * sb; }
  else                       { targetA = mvDir * sa * turn; targetB = -mvDir * sb * turn; }
}

// ====================== AUTO DRIVE (testing without the laptop) ======================
// Drives forward. When something is in the way it just stops and waits.
// It sets off again by itself once the way has been clear for about 0.8 s.
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
  digitalWrite(TRIG_PIN, HIGH);                        // 10 microsecond ping
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  unsigned long us = pulseIn(ECHO_PIN, HIGH, 15000);   // wait up to 15 ms for the echo
  distanceMm = us ? (int)(us * 0.1715) : -1;           // -1 = nothing in range

  bool close = (distanceMm > 0 && distanceMm < STOP_MM);
  closeCount = close ? closeCount + 1 : 0;
  if (closeCount >= 2) {                               // two readings in a row, so one glitch can't stop us
    blocked = true;
    clearSince = 0;
  } else if (blocked && !close) {
    if (clearSince == 0) clearSince = millis();
    else if (millis() - clearSince > 800) blocked = false;   // clear for 0.8 s -> go again
  }
}

// ====================== FACE ======================
enum FaceMode { FACE_IDLE, FACE_HAPPY, FACE_ALERT, FACE_SLEEP };
FaceMode faceMode = FACE_IDLE;
int faceShown = -1;                     // which mode is currently drawn (-1 = redraw)
bool speaking = false;
unsigned long ipUntil = 0;              // while set, the address screen is showing
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

void happyEye(int cx) {                 // a circle with a shifted black circle cut out = arch
  tft.fillCircle(cx, 44, 18, GLOW);
  tft.fillCircle(cx, 53, 18, ST77XX_BLACK);
}

void eyesHappy() {
  clearEyes();
  happyEye(48);
  happyEye(112);
}

void eyesAlert() {                      // tall, wide-awake eyes
  clearEyes();
  tft.fillRoundRect(48 - 9, 20, 18, 40, 8, GLOW);
  tft.fillRoundRect(112 - 9, 20, 18, 40, 8, GLOW);
}

void sleepyEye(int cx) {                // a downward curve, like a closed eye
  tft.fillCircle(cx, 34, 18, GLOW);
  tft.fillCircle(cx, 25, 18, ST77XX_BLACK);
}

void eyesSleepy() {
  clearEyes();
  sleepyEye(48);
  sleepyEye(112);
}

void drawMouth(int h) {                 // mouth lives below the eyes (y 67-79)
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

  if (ipUntil) return;                  // address screen is showing

  // Eyes: react when the mode changes
  if ((int)faceMode != faceShown) {
    faceShown = (int)faceMode;
    step = 0;
    next = 0;
    zStep = 0;
    zNext = 0;
    tft.fillRect(139, 18, 20, 45, ST77XX_BLACK);       // clear the Z area
    if (faceMode == FACE_HAPPY) eyesHappy();
    else if (faceMode == FACE_ALERT) eyesAlert();
    else if (faceMode == FACE_SLEEP) eyesSleepy();
  }

  // Idle animation (non-blocking)
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

  // Sleeping: Zs float up, one more every 0.7 s, then start again
  if (faceMode == FACE_SLEEP && millis() >= zNext) {
    tft.fillRect(139, 18, 20, 45, ST77XX_BLACK);
    tft.setTextColor(GLOW);
    if (zStep >= 1) { tft.setTextSize(1); tft.setCursor(142, 48); tft.print("Z"); }
    if (zStep >= 2) { tft.setTextSize(2); tft.setCursor(140, 24); tft.print("Z"); }
    zStep = (zStep + 1) % 4;
    zNext = millis() + 700;
  }

  // Mouth: moves while speaking
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
  if (mvMode != MV_NONE) stopMove(3);   // an old-style command cancels any /move or /turn
  drive(cmd, ms, speed);
  server.send(200, "text/plain", "ok");
}

void handleMove() {
  wake();
  float cm = server.arg("cm").toFloat();
  if (fabs(cm) < 1) { stopMove(3); server.send(200, "text/plain", "ok"); return; }
  cm = constrain(cm, -500.0f, 500.0f);
  startMove(MV_STRAIGHT, cm > 0 ? 1 : -1, fabs(cm));
  server.send(200, "text/plain", "ok");
}

void handleTurn() {
  wake();
  float deg = server.arg("deg").toFloat();
  if (fabs(deg) < 1) { stopMove(3); server.send(200, "text/plain", "ok"); return; }
  deg = constrain(deg, -720.0f, 720.0f);
  startMove(MV_TURN, deg > 0 ? 1 : -1, fabs(deg));   // positive = left
  server.send(200, "text/plain", "ok");
}

void handleCalib() {
  if (server.hasArg("cm")   && server.arg("cm").toFloat()   > 0.0001) calCm  = server.arg("cm").toFloat();
  if (server.hasArg("deg")  && server.arg("deg").toFloat()  > 0.0001) calDeg = server.arg("deg").toFloat();
  if (server.hasArg("bias")) calBias = constrain(server.arg("bias").toFloat(), -0.5f, 0.5f);
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
  j += ",\"busy\":";
  j += (mvMode != MV_NONE) ? "1" : "0";
  j += ",\"res\":";
  j += String(mvResult);
  j += ",\"enc\":";
  j += ENCODERS ? "1" : "0";
  j += ",\"ta\":";
  j += String(ticksA);
  j += ",\"tb\":";
  j += String(ticksB);
  j += ",\"cm\":";
  j += String(calCm, 4);
  j += ",\"deg\":";
  j += String(calDeg, 4);
  j += ",\"bias\":";
  j += String(calBias, 4);
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
  WiFi.mode(WIFI_AP);                   // fallback: the rover's own Wi-Fi
  WiFi.softAP(AP_NAME, AP_PASS);
  label = "Join Wi-Fi ROVER, then:";
  return WiFi.softAPIP().toString();
}

// ====================== MAIN ======================
void setup() {
  Serial.begin(115200);

  // Screen first
  SPI.begin(SCK, MISO, MOSI, -1);
  tft.initR(INITR_MINI160x80);
  tft.setSPISpeed(10000000);
  tft.setRotation(3);
  GLOW = tft.color565(120, 230, 255);
  tft.fillScreen(ST77XX_BLACK);
  drawBrackets();
  eyesOpen(0);

  // Wheels
  pinMode(A1A, OUTPUT);
  pinMode(A1B, OUTPUT);
  pinMode(B1A, OUTPUT);
  pinMode(B1B, OUTPUT);
  writeMotorA(0);
  writeMotorB(0);

  // Wheel encoders (only if switched on above)
  if (ENCODERS) {
    pinMode(ENC_A_PIN, INPUT);
    pinMode(ENC_B_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(ENC_A_PIN), isrA, RISING);
    attachInterrupt(digitalPinToInterrupt(ENC_B_PIN), isrB, RISING);
  }

  // Ultrasonic sensor
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Wi-Fi, then show the address on the screen
  String label;
  String ip = connectWifi(label);
  Serial.print(label);
  Serial.print(" ");
  Serial.println(ip);
  showText(label.c_str(), ip);
  ipUntil = millis() + 8000;

  server.on("/drive", handleDrive);
  server.on("/move", handleMove);
  server.on("/turn", handleTurn);
  server.on("/calib", handleCalib);
  server.on("/speak", handleSpeak);
  server.on("/face", handleFace);
  server.on("/status", handleStatus);
  server.begin();

  lastActivity = millis();
}

void loop() {
  server.handleClient();
  updateSensor();

  // After the address screen: put the face back
  if (ipUntil && millis() > ipUntil) {
    ipUntil = 0;
    tft.fillScreen(ST77XX_BLACK);
    drawBrackets();
    faceShown = -1;
  }

  // The face reacts to obstacles by itself (and wakes up if someone steps close)
  static bool wasBlocked = false;
  if (blocked && !wasBlocked) { faceMode = FACE_ALERT; lastActivity = millis(); }
  if (!blocked && wasBlocked) { faceMode = FACE_IDLE;  lastActivity = millis(); }
  wasBlocked = blocked;

  // Fall asleep when there's nothing to do
  if (faceMode == FACE_IDLE && !speaking && curA == 0 && curB == 0 && mvMode == MV_NONE &&
      millis() - lastActivity > SLEEP_AFTER_MS) {
    faceMode = FACE_SLEEP;
  }

  if (AUTO_DRIVE) autoDrive();
  updateMove();
  updateMotors();
  updateFace();
}
