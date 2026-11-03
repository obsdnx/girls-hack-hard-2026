// PATH TEST v2 for Genesis Mini + L9110S driver (no Wi-Fi, no camera, no screen)
// Waits 5 seconds, then: forward 5 steps -> turn left -> forward 2 steps -> STOP. Runs once only.
// Press the RESET button on the board to run it again.
//
// What changed to make it go around less:
//   - Much shorter steps and turn (less time = less drifting)
//   - The 5 steps are ONE smooth drive with no stopping in between (every stop and start made it swing)
//   - Short extra-power kick at each start so both motors begin together
//   - Wheel B is the slow one, so wheel A is turned down a bit (A_SCALE) to match it

// Driver pins
#define A1A 1
#define A1B 2
#define B1A 3
#define B1B 4

// ---------- SETTINGS ----------
int  SPEED     = 200;     // 0-255. Higher is steadier. If it is too fast try 180
int  STEP_MS   = 250;     // ONE step in milliseconds (was 600). Smaller = shorter path
int  TURN_MS   = 250;     // the turn (was 700). Bigger = turns more
bool TURN_LEFT = true;    // false = turn right

// Both wheels were going backwards without these, so keep them true
bool A_REVERSED = true;
bool B_REVERSED = true;

// Wheel B is slower, so wheel A gets less power. 1.0 = full.
// If it still curves RIGHT, lower A_SCALE (0.85, 0.8). If it now curves LEFT, raise it (0.95, 1.0).
float A_SCALE = 0.90;
float B_SCALE = 1.0;

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

void stopAll() { motorA(0); motorB(0); }

// Drive both wheels for a time. A short kick at the start gets both motors turning together.
void run(int a, int b, int ms) {
  int ka = a > 0 ? constrain(a + 55, 0, 255) : constrain(a - 55, -255, 0);
  int kb = b > 0 ? constrain(b + 55, 0, 255) : constrain(b - 55, -255, 0);
  motorA(ka);
  motorB(kb);
  delay(80);                    // kick
  motorA(a);
  motorB(b);
  delay(ms);
  stopAll();
}

void setup() {
  Serial.begin(115200);
  pinMode(A1A, OUTPUT);
  pinMode(A1B, OUTPUT);
  pinMode(B1A, OUTPUT);
  pinMode(B1B, OUTPUT);
  stopAll();

  Serial.println("Path test starts in 5 seconds...");
  delay(5000);

  Serial.println("Forward 5 steps");
  run(SPEED, SPEED, 5 * STEP_MS);        // one smooth drive, no stops
  delay(400);

  Serial.println("Turn");
  int dir = TURN_LEFT ? 1 : -1;
  run(-dir * SPEED, dir * SPEED, TURN_MS);
  delay(400);

  Serial.println("Forward 2 steps");
  run(SPEED, SPEED, 2 * STEP_MS);

  stopAll();
  Serial.println("DONE. Press RESET to run again.");
}

void loop() {
  // nothing - the test only runs once
}
