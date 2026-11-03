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

Making the path accurate (do this once, it takes about 10 minutes):
  python rover_brain.py --calibrate          the rover drives test moves, you measure with a tape measure,
                                             and the numbers are saved in rover_calibration.json
  python rover_brain.py --move 100           test: drive 100 cm forward
  python rover_brain.py --turn 90            test: turn 90 degrees left (use -90 for right)

Camera: CAMERA = 0 is the laptop webcam. For the Reolink stand camera put its stream address in CAMERA.
  Use the MAIN stream so QR cards can be read from a distance, e.g.
  "rtsp://admin:PASSWORD@CAMERA_IP:554/h264Preview_01_main"   (some models: h265Preview_01_main)
  Check it opens in VLC first (Media > Open Network Stream). RTSP may need switching on in the camera settings.
"""

import os
import sys
import json
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
# Route steps are real distances now:
#     ("go", 100)              drive forward 100 cm  (use a negative number to reverse)
#     ("turn", "left", 90)     turn on the spot 90 degrees ("right" works too)
# Measure your table layout with a tape measure and type the numbers in.
# (Run --calibrate once first so 100 cm really is 100 cm.)
DESTINATIONS = {
    "GATE12": {"name": "Gate 12",  "route": [("go", 50), ("turn", "left", 90), ("go", 30)]},
    "GATE5":  {"name": "Gate 5",   "route": [("go", 40), ("turn", "right", 90), ("go", 40)]},
    "CAFE":   {"name": "the cafe", "route": [("go", 70)]},
}

# Only used if the rover is still running the OLD firmware (no /move): rough conversion to timed driving
OLD_CM_PER_SEC = 20.0
OLD_DEG_PER_SEC = 200.0
CALIBRATION_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "rover_calibration.json")

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


def drive_for(cmd, seconds, progress_units=0.0):
    """OLD firmware only: drive for a number of seconds. Pauses while something is in the way."""
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
        state["done_s"] += progress_units * 0.2 / max(seconds, 0.2)
    rover("/drive", cmd="stop")


def step_units(s):
    """How big a route step is, just for the map progress bar."""
    return abs(s[1]) if s[0] == "go" else abs(s[2]) / 3.0


def move_and_wait(path, units, **params):
    """Tell the rover to do one precise move, then wait until it says it's finished.
    The rover measures the move itself. While something is in the way it just waits."""
    if rover(path, **params) is None:
        abort.set()
        return False
    start = time.time()
    est = max(2.0, units / (OLD_CM_PER_SEC if path == "/move" else OLD_DEG_PER_SEC * 3.0)) * 3 + 15
    blocked_since = None
    fails = 0
    base = state["done_s"]
    time.sleep(0.25)                                  # give the rover a moment to start
    while not abort.is_set():
        st = rover("/status")
        if st is None:
            fails += 1
            if fails >= 5:
                print("!! Lost contact with the rover.")
                abort.set()
                state["stage"] = "idle"
                return False
            time.sleep(0.2)
            continue
        fails = 0
        if not st.get("busy"):
            if st.get("res") == 2:
                speak("My wheels are stuck. Can someone help me please?")
                abort.set()
                state["stage"] = "idle"
                return False
            state["done_s"] = base + units
            return True
        state["done_s"] = base + units * min(0.95, (time.time() - start) / est * 3)
        if path == "/move" and st.get("blocked") and float(params.get("cm", 1)) > 0:
            if blocked_since is None:
                blocked_since = time.time()
                speak("Excuse me, something is in my way.")
            elif time.time() - blocked_since > 20:
                rover("/drive", cmd="stop")
                speak("I cannot get past. Please clear the way.")
                abort.set()
                state["stage"] = "idle"
                return False
        else:
            blocked_since = None
        if time.time() - start > est:
            rover("/drive", cmd="stop")
            print("!! The move took far too long, stopping.")
            abort.set()
            state["stage"] = "idle"
            return False
        time.sleep(0.15)
    rover("/drive", cmd="stop")
    return False


def rover_has_moves():
    st = rover("/status")
    return bool(st and "busy" in st), st


def do_step(s, new_firmware):
    units = step_units(s)
    if s[0] == "go":
        cm = s[1]
        if new_firmware:
            move_and_wait("/move", units, cm=cm)
        else:
            drive_for("forward" if cm > 0 else "back", abs(cm) / OLD_CM_PER_SEC, units)
    else:
        deg = abs(s[2]) * (1 if s[1] == "left" else -1)
        if new_firmware:
            move_and_wait("/turn", units, deg=deg)
        else:
            drive_for(s[1], abs(deg) / OLD_DEG_PER_SEC, units)


def run_route(code):
    dest = DESTINATIONS[code]
    steps = dest["route"]
    state["total_s"] = max(0.1, sum(step_units(s) for s in steps))
    state["done_s"] = 0.0
    new_firmware, _ = rover_has_moves()
    sync_calibration()
    for s in steps:
        if abort.is_set():
            return
        do_step(s, new_firmware)
    if abort.is_set():
        return
    rover("/face", mood="happy")
    speak(f"We have arrived at {dest['name']}. Have a lovely day!")
    time.sleep(2)
    rover("/face", mood="idle")
    state["stage"] = "idle"
    state["pending"] = None
    state["message"] = "Trip finished. Show an open hand to start again."


# ============================ CALIBRATION ============================
def load_cal():
    try:
        with open(CALIBRATION_FILE) as f:
            return json.load(f)
    except Exception:
        return {}


def save_cal(cal):
    with open(CALIBRATION_FILE, "w") as f:
        json.dump(cal, f, indent=2)


def cal_mode(st):
    return "encoders" if st.get("enc") else "timing"


def sync_calibration():
    """Send the saved numbers to the rover (it forgets them when it is switched off)."""
    st = rover("/status")
    if not st or "busy" not in st:
        return
    saved = load_cal().get(cal_mode(st))
    if saved:
        rover("/calib", cm=saved["cm"], deg=saved["deg"], bias=saved["bias"])
        print("Calibration sent to the rover:", saved)


def ask_number(question):
    while True:
        a = input(question + " ").strip()
        try:
            return float(a)
        except ValueError:
            print("  Please type just a number, like 95 or -4.")


def wait_idle(timeout=90):
    time.sleep(0.4)
    t = time.time()
    while time.time() - t < timeout:
        st = rover("/status")
        if st and not st.get("busy"):
            return True
        time.sleep(0.2)
    return False


def calibrate():
    st = rover("/status")
    if not st or "busy" not in st:
        print("Can't talk to the new firmware. Upload rover_firmware.ino first, join the ROVER Wi-Fi, then try again.")
        return
    mode = cal_mode(st)
    cal = load_cal()
    cur = cal.get(mode) or {"cm": st["cm"], "deg": st["deg"], "bias": st["bias"]}
    rover("/calib", cm=cur["cm"], deg=cur["deg"], bias=cur["bias"])
    print(f"\nCALIBRATION ({mode} mode)\nYou need: a tape measure, and a clear straight line of at least 1.2 m.\n")

    print("PART 1 - drive straight 100 cm")
    sign, prev_side = 1.0, None
    for attempt in range(1, 6):
        input(f"  Round {attempt}: put the rover on the start line pointing along the tape, then press Enter. It will drive 100 cm. ")
        rover("/move", cm=100)
        wait_idle()
        dist = ask_number("  How far did it really go (cm)?")
        side = ask_number("  How far did it drift sideways? (0 = dead straight, negative = drifted LEFT, positive = drifted RIGHT)")
        if dist > 5:
            cur["cm"] *= dist / 100.0
        if mode == "timing":                           # encoders keep it straight by themselves
            if prev_side is not None and abs(side) > prev_side * 1.1:
                sign = -sign                           # that made it worse: try the other wheel
            prev_side = abs(side)
            step = max(-0.15, min(0.15, 0.003 * side * sign))
            cur["bias"] = max(-0.4, min(0.4, cur["bias"] + step))
        rover("/calib", cm=cur["cm"], deg=cur["deg"], bias=cur["bias"])
        cal[mode] = cur
        save_cal(cal)
        good = abs(dist - 100) <= 3 and (mode == "encoders" or abs(side) <= 3)
        print(f"  -> now distance factor {cur['cm']:.4f}, wheel balance {cur['bias']:.3f}")
        if good:
            print("  Straight and the right length. Part 1 done.\n")
            break

    print("PART 2 - turn 90 degrees to the left")
    for attempt in range(1, 4):
        input(f"  Round {attempt}: put the rover down with a straight edge or tape line pointing ahead, then press Enter. ")
        rover("/turn", deg=90)
        wait_idle()
        ang = ask_number("  How many degrees did it really turn? (about 90 is perfect; if it turned RIGHT type a negative number)")
        if ang < 0:
            print("  It turned the wrong way. In rover_firmware.ino set SWAP_TURN = true, upload, and run --calibrate again.")
            return
        if ang > 5:
            cur["deg"] *= ang / 90.0
        rover("/calib", cm=cur["cm"], deg=cur["deg"], bias=cur["bias"])
        cal[mode] = cur
        save_cal(cal)
        if abs(ang - 90) <= 5:
            print("  Good. Part 2 done.\n")
            break

    print("Saved to", CALIBRATION_FILE)
    print("rover_brain.py now sends these numbers to the rover before every trip.")


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
    args = sys.argv[1:]

    def take(flag):                      # reads "--flag value" and removes both
        if flag in args:
            i = args.index(flag)
            val = args[i + 1] if i + 1 < len(args) else None
            del args[i:i + 2]
            return val
        return None

    move_cm, turn_deg = take("--move"), take("--turn")
    do_qr = "--make-qr" in args
    do_cal = "--calibrate" in args
    given = [a for a in args if not a.startswith("--")]
    if given:
        ROVER = "http://" + given[0]
    if do_qr:
        make_qr()
        sys.exit(0)
    print("Rover address:", ROVER)
    if do_cal:
        calibrate()
        sys.exit(0)
    if move_cm is not None or turn_deg is not None:
        sync_calibration()
        if move_cm is not None:
            print("Driving", move_cm, "cm ...")
            rover("/move", cm=float(move_cm))
        else:
            print("Turning", turn_deg, "degrees ...")
            rover("/turn", deg=float(turn_deg))
        wait_idle()
        print("Done. Measure it!")
        sys.exit(0)
    sync_calibration()
    main()
