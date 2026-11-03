// WHEEL TEST for Genesis Mini + L9110S driver
// Runs on a loop: tests each wheel alone, then both together.
// Open Serial Monitor at 115200 baud to see what it is doing.
// PUT THE ROVER ON A BOX so the wheels are in the air for the first run!

// Driver pins (raw GPIO, same as the main rover code)
#define A1A 1
#define A1B 2
#define B1A 3
#define B1B 4

// Settings you can change
int  TEST_SPEED = 150;      // 0-255. If a wheel doesn't spin, try 180
bool FLIP_WHEELS = true;    // same as the main code: true = "forward" is forward
float WHEEL_A_SCALE = 1.0;  // if rover drifts, lower the faster wheel (e.g. 0.9)
float WHEEL_B_SCALE = 1.0;

void motorA(int s) {
  s = (int)(s * WHEEL_A_SCALE);
  if (FLIP_WHEELS) s = -s;
  if (s >= 0) { analogWrite(A1A, 0);  analogWrite(A1B, s); }
  else        { analogWrite(A1B, 0);  analogWrite(A1A, -s); }
}

void motorB(int s) {
  s = (int)(s * WHEEL_B_SCALE);
  if (FLIP_WHEELS) s = -s;
  if (s >= 0) { analogWrite(B1B, 0);  analogWrite(B1A, s); }
  else        { analogWrite(B1A, 0);  analogWrite(B1B, -s); }
}

void stopAll() {
  motorA(0);
  motorB(0);
}

void step(const char* name, int a, int b, int ms) {
  Serial.println(name);
  motorA(a);
  motorB(b);
  delay(ms);
  stopAll();
  Serial.println("  stop");
  delay(1500);
}

void setup() {
  Serial.begin(115200);
  pinMode(A1A, OUTPUT);
  pinMode(A1B, OUTPUT);
  pinMode(B1A, OUTPUT);
  pinMode(B1B, OUTPUT);
  stopAll();
  Serial.println("Wheel test starts in 5 seconds...");
  delay(5000);
}

void loop() {
  Serial.println("---- NEW ROUND ----");
  step("1. Wheel A only, forward",  TEST_SPEED, 0, 2000);
  step("2. Wheel A only, backward", -TEST_SPEED, 0, 2000);
  step("3. Wheel B only, forward",  0, TEST_SPEED, 2000);
  step("4. Wheel B only, backward", 0, -TEST_SPEED, 2000);
  step("5. BOTH forward",           TEST_SPEED, TEST_SPEED, 3000);
  step("6. BOTH backward",          -TEST_SPEED, -TEST_SPEED, 3000);
  step("7. Spin left",              -TEST_SPEED, TEST_SPEED, 1500);
  step("8. Spin right",             TEST_SPEED, -TEST_SPEED, 1500);
  Serial.println("Round done. Repeating in 5 seconds...");
  delay(5000);
}
