#!/usr/bin/env python3
"""
ROVER BRAIN - runs on your laptop
=====================================================================
The story it runs ("reception desk" demo):
  1. Show an open hand to the camera   -> rover says hello and asks for a destination card
  2. Hold up a QR card (GATE12 ...)    -> rover reads it out and asks for a thumbs up
  3. Thumbs up                         -> "Follow me!" - rover drives the route, stops for obstacles
  4. Arrival                           -> happy face, announces it, then goes back to idle (and later sleeps)
  Fist at any time = emergency stop.   Peace sign = HELP.

Setup (once):
  pip install opencv-python mediapipe requests numpy "qrcode[pil]"      (add pyttsx3 if you're not on a Mac)
  Use Python 3.10 - 3.12 (mediapipe doesn't always have wheels for newer Python).

Run:
  python rover_brain.py --make-qr            makes card_GATE12.png etc. - print them (8-10 cm wide)
  python rover_brain.py 172.20.10.5          172.20.10.5 = the address shown on the rover's screen
  (press q in the camera window to quit)

Camera: CAMERA = 0 is the laptop webcam. For the Reolink stand camera put its stream address in CAMERA.
  Use the MAIN stream so QR cards can be read from a distance, e.g.
  "rtsp://admin:PASSWORD@CAMERA_IP:554/h264Preview_01_main"   (some models: h265Preview_01_main)
  Check it opens in VLC first (Media > Open Network Stream). RTSP may need switching on in the camera settings.
"""

import sys
import time
import queue
import threading
import subprocess

import cv2
import numpy as np
import requests

# ============================ SETTINGS ============================
ROVER = "http://192.168.4.1"    # the rover's address. Overridden if you type one after the command.
CAMERA = 0                      # 0 = laptop webcam, or the Reolink RTSP address (see above)
SPEED = None                    # None = use the speed set in the rover firmware, or a number 0-255
STABLE_FRAMES = 6               # a hand sign must be held for this many checks to count
COOLDOWN = {"HELLO": 6.0, "YES": 4.0, "STOP": 3.0, "HELP": 8.0}   # seconds before a sign can repeat

# Destinations. The text inside each QR card must match the name on the left exactly.
# Route steps:  ("go", seconds)  drive forward     ("turn", "left"/"right", seconds)
# >>> The times depend on your floor and batteries: test and adjust them! <<<
DESTINATIONS = {
    "GATE12": {"name": "Gate 12",  "route": [("go", 3.0), ("turn", "left", 0.8), ("go", 2.0)]},
    "GATE5":  {"name": "Gate 5",   "route": [("go", 2.0), ("turn", "right", 0.8), ("go", 2.5)]},
    "CAFE":   {"name": "the cafe", "route": [("go", 4.0)]},
}

# Where things sit on the little map window (just for the picture)
MAP_POS = {"START": (50, 220), "GATE12": (270, 70), "GATE5": (270, 220), "CAFE": (160, 70)}

# ============================ SHARED STATE ============================
state = {
    "stage": "idle",       # idle -> awaiting_card -> awaiting_yes -> guiding
    "pending": None,       # the destination chosen
    "message": "Show an open hand to wake me up",
    "done_s": 0.0,         # seconds of the route driven so far (for the map)
    "total_s": 1.0,
}
cmd_q = queue.Queue()
abort = threading.Event()
_tts = None
_warned_offline = False


# ============================ TALKING TO THE ROVER ============================
def rover(path, **params):
    """Send a command to the rover. Returns the reply (a dict for /status) or None if unreachable."""
    global _warned_offline
    try:
        r = requests.get(ROVER + path, params=params, timeout=0.8)
        return r.json() if path == "/status" else True
    except Exception:
        if not _warned_offline:
            print("!! Can't reach the rover at", ROVER, "- check the address and that the laptop is on the same Wi-Fi.")
            _warned_offline = True
        return None


def speak(text):
    """Say it out loud, and make the rover's mouth move while we do."""
    print("SAY:", text)
    state["message"] = text
    rover("/speak", on=1)
    try:
        if sys.platform == "darwin":
            subprocess.run(["say", text])                # built into macOS
        else:
            global _tts
            if _tts is None:
                import pyttsx3
                _tts = pyttsx3.init()
            _tts.say(text)
            _tts.runAndWait()
    except Exception as e:
        print("Speech problem:", e)
        time.sleep(1.5)
    finally:
        rover("/speak", on=0)


def emergency_stop():
    abort.set()
    state["stage"] = "idle"
    state["pending"] = None
    rover("/drive", cmd="stop")


# ============================ DRIVING ============================
def is_blocked():
    st = rover("/status")
    return bool(st and st.get("blocked"))


def drive_params(cmd):
    p = {"cmd": cmd, "ms": 350}          # short commands, repeated: the rover stops if we vanish
    if SPEED is not None:
        p["speed"] = SPEED
    return p


def drive_for(cmd, seconds):
    """Drive for a number of seconds. Pauses (without using up time) while something is in the way."""
    done = 0.0
    blocked_since = None
    while done < seconds and not abort.is_set():
        if cmd == "forward" and is_blocked():
            rover("/drive", cmd="stop")
            if blocked_since is None:
                blocked_since = time.time()
                speak("Excuse me, something is in my way.")
            elif time.time() - blocked_since > 20:
                speak("I cannot get past. Please clear the way.")
                abort.set()
                state["stage"] = "idle"
                break
            time.sleep(0.2)
            continue
        blocked_since = None
        rover("/drive", **drive_params(cmd))
        time.sleep(0.2)
        done += 0.2
        state["done_s"] += 0.2
    rover("/drive", cmd="stop")


def run_route(code):
    dest = DESTINATIONS[code]
    steps = dest["route"]
    state["total_s"] = max(0.1, sum(s[1] if s[0] == "go" else s[2] for s in steps))
    state["done_s"] = 0.0
    for s in steps:
        if abort.is_set():
            return
        if s[0] == "go":
            drive_for("forward", s[1])
        else:
            drive_for(s[1], s[2])
    if abort.is_set():
        return
    rover("/face", mood="happy")
    speak(f"We have arrived at {dest['name']}. Have a lovely day!")
    time.sleep(2)
    rover("/face", mood="idle")
    state["stage"] = "idle"
    state["pending"] = None
    state["message"] = "Trip finished. Show an open hand to start again."


# ============================ THE WORKER (does the slow things) ============================
def handle(item):
    kind = item[0]
    stage = state["stage"]
    if kind == "hello" and stage != "guiding":
        abort.clear()
        state["stage"] = "awaiting_card"
        state["pending"] = None
        rover("/face", mood="happy")
        speak("Hello! Hold up a destination card and I will read it.")
        rover("/face", mood="idle")
    elif kind == "card" and stage in ("awaiting_card", "awaiting_yes") and item[1] in DESTINATIONS:
        code = item[1]
        state["pending"] = code
        state["stage"] = "awaiting_yes"
        speak(f"{DESTINATIONS[code]['name']}. Show me a thumbs up and I will take you there.")
    elif kind == "yes" and stage == "awaiting_yes":
        abort.clear()
        state["stage"] = "guiding"
        speak("Great. Follow me!")
        if not abort.is_set():
            run_route(state["pending"])
    elif kind == "help":
        rover("/face", mood="alert")
        speak("I am calling for assistance. Please stay with me.")
        rover("/face", mood="idle")
    elif kind == "stopped":
        speak("Okay, stopping.")


def worker():
    while True:
        item = cmd_q.get()
        try:
            handle(item)
        except Exception as e:
            print("worker error:", e)


# ============================ HAND SIGNS ============================
def _dist(a, b):
    return ((a.x - b.x) ** 2 + (a.y - b.y) ** 2) ** 0.5


def fingers_up(lm):
    """[thumb, index, middle, ring, pinky] -> True if that finger is stretched out."""
    wrist = lm[0]
    ups = [_dist(lm[4], lm[17]) > _dist(lm[3], lm[17]) * 1.1]        # thumb
    for tip, pip in ((8, 6), (12, 10), (16, 14), (20, 18)):           # the four fingers
        ups.append(_dist(lm[tip], wrist) > _dist(lm[pip], wrist) * 1.1)
    return ups


def classify(lm):
    """lm = the 21 hand points from MediaPipe. Returns HELLO / YES / STOP / HELP or None."""
    t, i, m, r, p = fingers_up(lm)
    if t and i and m and r and p:
        return "HELLO"                                    # open hand
    if not (i or m or r or p):
        if t and lm[4].y < lm[0].y - 0.12 and lm[4].y < lm[2].y:
            return "YES"                                  # thumbs up
        if not t:
            return "STOP"                                 # fist
    if i and m and not r and not p:
        return "HELP"                                     # peace sign
    return None


def fire(gesture):
    if gesture == "STOP":
        emergency_stop()
        cmd_q.put(("stopped",))
    elif gesture == "HELLO":
        cmd_q.put(("hello",))
    elif gesture == "YES":
        cmd_q.put(("yes",))
    elif gesture == "HELP":
        cmd_q.put(("help",))


# ============================ MAP WINDOW ============================
def draw_map():
    img = np.full((280, 340, 3), 30, np.uint8)
    chosen = state["pending"]
    start = MAP_POS["START"]
    for code in DESTINATIONS:
        if code in MAP_POS:
            col = (80, 220, 255) if code == chosen else (90, 90, 90)
            cv2.line(img, start, MAP_POS[code], col, 5 if code == chosen else 3)
    cv2.circle(img, start, 12, (200, 200, 200), -1)
    cv2.putText(img, "START", (start[0] - 25, start[1] + 32), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (230, 230, 230), 1)
    for code, d in DESTINATIONS.items():
        if code in MAP_POS:
            x, y = MAP_POS[code]
            cv2.circle(img, (x, y), 14, (80, 220, 255) if code == chosen else (110, 110, 110), -1)
            cv2.putText(img, d["name"], (x - 30, y - 22), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (230, 230, 230), 1)
    pos = start
    if state["stage"] == "guiding" and chosen in MAP_POS:
        f = min(1.0, state["done_s"] / state["total_s"])
        ex, ey = MAP_POS[chosen]
        pos = (int(start[0] + (ex - start[0]) * f), int(start[1] + (ey - start[1]) * f))
    elif state["stage"] == "idle" and chosen is None:
        pos = start
    cv2.circle(img, pos, 7, (0, 0, 255), -1)                  # red dot = the rover
    cv2.putText(img, "MAP", (10, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2)
    return img


# ============================ QR CARDS ============================
def make_qr():
    import qrcode
    for code in DESTINATIONS:
        qrcode.make(code, box_size=14, border=4).save(f"card_{code}.png")
        print("made", f"card_{code}.png")
    print("Print them about 8-10 cm wide. Hold one up to the camera when the rover asks.")


# ============================ MAIN LOOP ============================
def main():
    import mediapipe as mp
    cap = cv2.VideoCapture(CAMERA)
    if isinstance(CAMERA, str):
        cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)                  # less delay on network streams
    if not cap.isOpened():
        print("Can't open the camera. Try CAMERA = 1, check permissions, or test the stream in VLC.")
        return

    qr = cv2.QRCodeDetector()
    mp_hands = mp.solutions.hands
    hands = mp_hands.Hands(max_num_hands=1, min_detection_confidence=0.6, min_tracking_confidence=0.5)
    drawer = mp.solutions.drawing_utils

    threading.Thread(target=worker, daemon=True).start()

    stable, stable_n = None, 0
    last_fire = {}
    last_qr, last_qr_t = "", 0.0
    frame_n, fails = 0, 0

    while True:
        ok, frame = cap.read()
        if not ok:
            fails += 1
            if fails > 50:
                print("Camera stopped sending pictures.")
                break
            time.sleep(0.05)
            continue
        fails = 0
        frame_n += 1
        h, w = frame.shape[:2]
        show = frame if w <= 960 else cv2.resize(frame, (960, int(h * 960 / w)))
        sc = show.shape[1] / w

        # ---- QR cards (every 3rd picture, full size so they can be read from further away) ----
        if frame_n % 3 == 0:
            data, pts, _ = qr.detectAndDecode(frame)
            if pts is not None and len(pts):
                cv2.polylines(show, [(pts * sc).astype(int)], True, (0, 255, 0), 3)
            if data and (data != last_qr or time.time() - last_qr_t > 5):
                last_qr, last_qr_t = data, time.time()
                print("QR:", data)
                if data in DESTINATIONS:
                    cmd_q.put(("card", data))

        # ---- hand signs ----
        small = frame if w <= 640 else cv2.resize(frame, (640, int(h * 640 / w)))
        res = hands.process(cv2.cvtColor(small, cv2.COLOR_BGR2RGB))
        g = None
        if res.multi_hand_landmarks:
            hl = res.multi_hand_landmarks[0]
            drawer.draw_landmarks(show, hl, mp_hands.HAND_CONNECTIONS)
            g = classify(hl.landmark)
        stable_n = stable_n + 1 if g == stable else 1
        stable = g
        if g and stable_n >= STABLE_FRAMES and time.time() - last_fire.get(g, 0) > COOLDOWN[g]:
            last_fire[g] = time.time()
            print("SIGN:", g)
            fire(g)

        # ---- screens ----
        cv2.putText(show, f"Sign: {g}   Stage: {state['stage']}", (10, 30),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 255), 2)
        cv2.putText(show, state["message"][:70], (10, show.shape[0] - 15),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2)
        cv2.imshow("Rover camera", show)
        cv2.imshow("Map", draw_map())
        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

    emergency_stop()
    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    if "--make-qr" in sys.argv:
        make_qr()
        sys.exit(0)
    given = [a for a in sys.argv[1:] if not a.startswith("--")]
    if given:
        ROVER = "http://" + given[0]
    print("Rover address:", ROVER)
    main()
