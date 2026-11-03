// PATH + OBSTACLE for Genesis Mini + L9110S driver + HC-SR04
// =====================================================================
// A NEW, separate sketch. It does NOT replace rover_firmware.ino.
//
// What it does (once, then stops):
//   1. Waits 5 seconds
//   2. Drives forward
//   3. Sharp turn on the spot
//   4. Drives forward again, then STOPS
//
// Obstacle rule: if something is in front while driving forward, the rover STOPS and WAITS.
// It does not turn around. When the way has been clear for a moment it carries on, and the
// time it spent waiting is not counted, so it still drives the full distance.
// (It does not check for obstacles during the turn, because it turns on the spot.)
//
// No Wi-Fi, no camera, no screen. Press the RESET button on the board to run it again.
//
// Wiring: same as before.
//   Wheels (L9110S): A1A=GPIO1, A1B=GPIO2, B1A=GPIO3, B1B=GPIO4
//   HC-SR04: TRIG -> port 3 IO0 (GPIO 9), ECHO -> port 3 IO1 (GPIO 16), VCC -> 3V3, GND -> GND

// ---------- PINS ----------
#define A1A 1
#define A1B 2
#define B1A 3
#define B1B 4
#define TRIG_PIN 9
#define ECHO_PIN 16

// ---------- SETTINGS (change these, upload again) ----------
const int  SPEED        = 200;    // forward speed 0-255 (higher is steadier)
const int  TURN_SPEED   = 230;    // speed of the sharp turn
const bool TURN_LEFT    = true;   // false = turn right

const unsigned long START_DELAY_MS = 5000;   // wait before moving
const unsigned long FORWARD1_MS    = 1500;   // first straight part (longer = further)
const unsigned long TURN_MS        = 300;    // the sharp turn. Bigger = turns more. Tune until it is a right angle
const unsigned long FORWARD2_MS    = 700;    // second straight part

const int  STOP_MM      = 200;    // something closer than this (millimetres) = stop and wait
const unsigned long CLEAR_MS = 800;   // the way must be clear this long before it sets off again

// Both wheels went backwards without these, so keep them true
const bool A_REVERSED = true;
const bool B_REVERSED = true;

// Wheel B is the slower one, so wheel A gets less power. 1.0 = full.
// Still curves right? lower A_SCALE (0.85). Now curves left? raise it (0.95).
const float A_SCALE = 0.90;
const float B_SCALE = 1.0;

// ---------- motors ----------
void motorA(int s) {
  s = constrain((int)(s * A_SCALE), -255, 255);
  if (A_REVERSED) s = -s;
  if (s >= 0) { analogWrite(A1A, 0);  analogWrite(A1B, s); }
  else        { analogWrite(A1B, 0);  analogWrite(A1A, -s); }
}

void motorB(int s) {
  s = constrain((int)(s * B_SCALE), -255, 255);
  if (B_REVERSED) s = -s;
  if (s >= 0) { analogWrite(B1B, 0);  analogWrite(B1A, s); }
  else        { analogWrite(B1A, 0);  analogWrite(B1B, -s); }
}

int wantA = 0, wantB = 0;               // the speeds we want once the starting kick is over
unsigned long kickUntil = 0;
bool moving = false;

void stopAll() {
  motorA(0);
  motorB(0);
  moving = false;
  kickUntil = 0;
}

int boost(int x) {                      // a bit more power for a moment so both motors start together
  if (x > 0) return constrain(x + 55, 0, 255);
  if (x < 0) return constrain(x - 55, -255, 0);
  return 0;
}

void startMotors(int a, int b) {
  wantA = a;
  wantB = b;
  motorA(boost(a));
  motorB(boost(b));
  kickUntil = millis() + 80;
  moving = true;
}

// ---------- obstacle sensor ----------
bool blocked = false;
int distanceMm = -1;

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
  distanceMm = us ? (int)(us * 0.1715) : -1;           // -1 = nothing in range

  bool close = (distanceMm > 0 && distanceMm < STOP_MM);
  closeCount = close ? closeCount + 1 : 0;
  if (closeCount >= 2) {                               // two readings in a row, so one glitch can't stop us
    blocked = true;
    clearSince = 0;
  } else if (blocked && !close) {
    if (clearSince == 0) clearSince = millis();
    else if (millis() - clearSince > CLEAR_MS) blocked = false;
  }
}

// ---------- the path ----------
const int ST_WAIT = 0, ST_FWD1 = 1, ST_TURN = 2, ST_FWD2 = 3, ST_DONE = 4;
int state = ST_WAIT;
unsigned long stepRun = 0;              // how long this step has really been driving (waiting doesn't count)
unsigned long holdUntil = 0;            // short pause between steps
unsigned long lastLoop = 0;
bool wasBlocked = false;

void goTo(int next) {
  state = next;
  stepRun = 0;
  holdUntil = millis() + 400;           // short pause between steps
  stopAll();
  if (next == ST_FWD1) Serial.println("Forward");
  if (next == ST_TURN) Serial.println("Sharp turn");
  if (next == ST_FWD2) Serial.println("Forward again");
  if (next == ST_DONE) Serial.println("DONE. Press RESET to run again.");
}

void setup() {
  Serial.begin(115200);
  pinMode(A1A, OUTPUT);
  pinMode(A1B, OUTPUT);
  pinMode(B1A, OUTPUT);
  pinMode(B1B, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  stopAll();
  Serial.println("Path with obstacle stop. Starting in 5 seconds...");
  holdUntil = millis() + START_DELAY_MS;
  lastLoop = millis();
}

void loop() {
  unsigned long now = millis();
  unsigned long dt = now - lastLoop;
  lastLoop = now;
  if (dt > 100) dt = 100;

  updateSensor();

  // starting kick is over: go to the normal speed
  if (kickUntil && now >= kickUntil) {
    kickUntil = 0;
    motorA(wantA);
    motorB(wantB);
  }

  if (state == ST_DONE) return;
  if (now < holdUntil) return;
  if (state == ST_WAIT) { goTo(ST_FWD1); return; }

  bool forwardStep = (state == ST_FWD1 || state == ST_FWD2);
  unsigned long duration = (state == ST_FWD1) ? FORWARD1_MS : (state == ST_TURN) ? TURN_MS : FORWARD2_MS;

  // Something in the way while driving forward: stop and wait. No turning around.
  if (forwardStep && blocked) {
    if (moving) {
      stopAll();
      Serial.print("Obstacle at ");
      Serial.print(distanceMm);
      Serial.println(" mm - waiting");
    }
    return;                              // the step timer does not run while we wait
  }

  if (!moving) {
    if (state == ST_TURN) {
      int dir = TURN_LEFT ? 1 : -1;
      startMotors(-dir * TURN_SPEED, dir * TURN_SPEED);
    } else {
      startMotors(SPEED, SPEED);
    }
  }

  stepRun += dt;
  if (stepRun >= duration) {
    goTo(state == ST_FWD1 ? ST_TURN : state == ST_TURN ? ST_FWD2 : ST_DONE);
  }
}
