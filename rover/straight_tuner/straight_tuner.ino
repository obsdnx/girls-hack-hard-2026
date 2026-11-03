// STRAIGHT TUNER v2 for Genesis Mini + L9110S driver
// =================================================================
// The rover makes its own Wi-Fi called ROVER (password rover1234).
// Join it from your phone or laptop, open a browser and go to:   http://192.168.4.1
//
// What is different from v1 (why it should swerve less):
//   - Starts with a short full-power "kick" so both motors begin turning at the same moment
//     (small motors don't all start at the same power, which makes the rover swing at every start)
//   - Faster default speed (210). At low speed one motor can barely turn while the other runs
//   - Corrections help the weak wheel (more power) AND slow the strong one, instead of only slowing one
//   - Test buttons: wheel A alone, wheel B alone, and full power, so we can find out what is really wrong

#include <WiFi.h>
#include <WebServer.h>

// Driver pins
#define A1A 1
#define A1B 2
#define B1A 3
#define B1B 4

// Your rover goes backwards without these, so keep both true
const bool A_REVERSED = true;
const bool B_REVERSED = true;

int   speedNow = 210;            // 0-255, change with the page buttons
float trim = 0.0;                // > 0 = wheel A gets less power and B more. < 0 = the other way
int   dirSign = 1;               // flipped by the Swap button
float lastDelta = 0.0;
int   turnMs = 300;
bool  useTrim = true;            // the full power test switches this off
int   tgtA = 0, tgtB = 0;
unsigned long endAt = 0, kickUntil = 0;
bool running = false;

WebServer server(80);

float scaleA() { return useTrim ? 1.0 - trim / 2.0 : 1.0; }
float scaleB() { return useTrim ? 1.0 + trim / 2.0 : 1.0; }

void motorA(int s) {
  s = constrain((int)(s * scaleA()), -255, 255);
  if (A_REVERSED) s = -s;
  if (s >= 0) { analogWrite(A1A, 0);  analogWrite(A1B, s); }
  else        { analogWrite(A1B, 0);  analogWrite(A1A, -s); }
}

void motorB(int s) {
  s = constrain((int)(s * scaleB()), -255, 255);
  if (B_REVERSED) s = -s;
  if (s >= 0) { analogWrite(B1B, 0);  analogWrite(B1A, s); }
  else        { analogWrite(B1A, 0);  analogWrite(B1B, -s); }
}

void stopAll() {
  motorA(0);
  motorB(0);
  running = false;
  kickUntil = 0;
  useTrim = true;
}

int boost(int x) {               // a bit more power for the first moment, so the motor gets going
  if (x > 0) return constrain(x + 70, 0, 255);
  if (x < 0) return constrain(x - 70, -255, 0);
  return 0;
}

void startRun(int a, int b, unsigned long ms) {
  tgtA = a;
  tgtB = b;
  motorA(boost(a));
  motorB(boost(b));
  kickUntil = millis() + 150;
  endAt = millis() + ms;
  running = true;
}

String stateText() {
  String t = "";
  t += "SPEED = " + String(speedNow);
  t += "\nA_SCALE = " + String(1.0 - trim / 2.0, 2) + "   B_SCALE = " + String(1.0 + trim / 2.0, 2);
  t += "\nTURN_MS = " + String(turnMs);
  return t;
}

void sendState(const String& extra) {
  String t = extra;
  if (t.length()) t += "\n";
  t += stateText();
  server.send(200, "text/plain", t);
}

const char PAGE[] = R"rawliteral(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Rover tuner</title>
<style>
body{font-family:-apple-system,sans-serif;max-width:480px;margin:16px auto;padding:0 14px;background:#111;color:#eee}
h1{font-size:20px} h2{font-size:16px;margin:20px 0 4px} p{color:#aaa;font-size:14px;margin:4px 0}
button{display:block;width:100%;font-size:19px;padding:16px;margin:8px 0;border:0;border-radius:12px;background:#2a6;color:#fff}
button.b{background:#36c} button.r{background:#c43} button.g{background:#555}
.row{display:flex;gap:8px}.row button{flex:1}
#s{background:#222;padding:12px;border-radius:10px;font-family:monospace;font-size:15px;white-space:pre-wrap}
</style></head><body>
<h1>Rover tuner v2</h1>
<div id="s">...</div>

<h2>A. Check each wheel (rover in the air or on the floor)</h2>
<p>Each wheel should spin FORWARD for 2 seconds, and they should look equally fast.</p>
<div class="row"><button class="b" onclick="c('/testa')">Wheel A only</button><button class="b" onclick="c('/testb')">Wheel B only</button></div>
<button class="g" onclick="c('/full')">FULL POWER straight 3 s</button>

<h2>B. Straight line</h2>
<p>Put the rover on the start line. Press GO. Watch which way it curves.</p>
<button onclick="c('/go')">GO forward 3 seconds</button>
<div class="row"><button class="b" onclick="c('/veer?d=-1')">Veered LEFT</button><button class="b" onclick="c('/veer?d=1')">Veered RIGHT</button></div>
<button class="g" onclick="c('/swap')">That made it WORSE (swap)</button>
<div class="row"><button class="g" onclick="c('/speed?d=-10')">Slower</button><button class="g" onclick="c('/speed?d=10')">Faster</button></div>

<h2>C. Turn</h2>
<button onclick="c('/turn')">TURN left</button>
<div class="row"><button class="b" onclick="c('/turnms?d=25')">Too LITTLE</button><button class="b" onclick="c('/turnms?d=-25')">Too MUCH</button></div>

<button class="r" onclick="c('/stop')">STOP</button>
<button class="g" onclick="c('/reset')">Start again (reset numbers)</button>
<script>
function c(u){fetch(u).then(r=>r.text()).then(t=>{document.getElementById('s').innerText=t}).catch(e=>{document.getElementById('s').innerText='No connection - are you on the ROVER Wi-Fi?'})}
c('/state');
</script></body></html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  pinMode(A1A, OUTPUT);
  pinMode(A1B, OUTPUT);
  pinMode(B1A, OUTPUT);
  pinMode(B1B, OUTPUT);
  stopAll();

  WiFi.mode(WIFI_AP);
  WiFi.softAP("ROVER", "rover1234");
  Serial.print("Join Wi-Fi ROVER then open http://");
  Serial.println(WiFi.softAPIP().toString());

  server.on("/", []() { server.send(200, "text/html", PAGE); });
  server.on("/state", []() { sendState(""); });
  server.on("/go", []() { useTrim = true; startRun(speedNow, speedNow, 3000); sendState("Driving forward..."); });
  server.on("/testa", []() { useTrim = false; startRun(speedNow, 0, 2000); sendState("Wheel A only..."); });
  server.on("/testb", []() { useTrim = false; startRun(0, speedNow, 2000); sendState("Wheel B only..."); });
  server.on("/full", []() { useTrim = false; startRun(255, 255, 3000); sendState("Full power, no corrections..."); });
  server.on("/turn", []() { useTrim = true; startRun(-speedNow, speedNow, turnMs); sendState("Turning left..."); });
  server.on("/stop", []() { stopAll(); sendState("Stopped."); });
  server.on("/veer", []() {
    int d = server.arg("d").toInt();           // 1 = it went right, -1 = it went left
    lastDelta = 0.03 * d * dirSign;
    trim = constrain(trim + lastDelta, -0.6f, 0.6f);
    sendState("Nudged. Put it back on the line and press GO.");
  });
  server.on("/swap", []() {
    dirSign = -dirSign;
    trim = constrain(trim - 2 * lastDelta, -0.6f, 0.6f);   // undo the last nudge and go the other way
    lastDelta = -lastDelta;
    sendState("Swapped. Put it back on the line and press GO.");
  });
  server.on("/speed", []() {
    speedNow = constrain(speedNow + server.arg("d").toInt(), 100, 255);
    sendState("Speed changed.");
  });
  server.on("/turnms", []() {
    turnMs = constrain(turnMs + server.arg("d").toInt(), 50, 2000);
    sendState("Turn time changed. Press TURN.");
  });
  server.on("/reset", []() { trim = 0; dirSign = 1; lastDelta = 0; turnMs = 300; speedNow = 210; stopAll(); sendState("Reset."); });
  server.begin();
}

void loop() {
  server.handleClient();
  if (kickUntil && millis() >= kickUntil) {     // kick is over: go to the normal speed
    kickUntil = 0;
    if (running) { motorA(tgtA); motorB(tgtB); }
  }
  if (running && millis() >= endAt) stopAll();
}
