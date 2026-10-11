import cv2
import requests
import time
import math
from ultralytics import YOLO

# ---- Config ----
INDEX = 0
ROVER_IP = ""  # fill in after the rover connects to WiFi

# ---- Gate map ----
GATES = {
    "A1": [(0, 100), (0, 200), (-100, 200)],
    "A2": [(0, 100), (0, 200), (0, 300)],
    "A3": [(0, 100), (0, 200), (100, 200), (100, 300)],
    "B1": [(0, 100), (100, 100), (100, 200), (100, 300)],
    "B2": [(0, 100), (100, 100), (200, 100), (200, 200)],
    "B3": [(0, 100), (100, 100), (200, 100), (200, 200), (200, 300)],
}

# ---- Navigation settings ----
MOVE_SPEED = 200
TURN_SPEED = 150
MOVE_MS = 400
TURN_MS = 300
MOVE_SPEED_CM_S = 20
ARRIVAL_THRESHOLD = 15

# ---- Passenger tracking ----
FOLLOW_DISTANCE_MIN = 0.15
FOLLOW_DISTANCE_MAX = 0.60
LOST_TIMEOUT = 3.0

# ---- State ----
pos_x = 0.0
pos_y = 0.0
heading = 90
current_wp = 0
last_person_seen = time.time()

# ---- YOLO ----
model = YOLO("yolo11n.pt")
cap = cv2.VideoCapture(INDEX, cv2.CAP_AVFOUNDATION)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

if not cap.isOpened():
    raise SystemExit(f"Cannot open camera at index {INDEX}")


def send_cmd(cmd, ms=None, speed=None):
    if not ROVER_IP:
        print(f"  [would send: /drive?cmd={cmd}]")
        return
    params = {"cmd": cmd}
    if ms is not None:
        params["ms"] = ms
    if speed is not None:
        params["speed"] = speed
    try:
        requests.get(f"http://{ROVER_IP}/drive", params=params, timeout=0.5)
    except Exception:
        pass


def send_face(mood):
    if not ROVER_IP:
        return
    try:
        requests.get(f"http://{ROVER_IP}/face", params={"mood": mood}, timeout=0.3)
    except Exception:
        pass


def send_speak(on):
    if not ROVER_IP:
        return
    try:
        requests.get(f"http://{ROVER_IP}/speak", params={"on": "1" if on else "0"}, timeout=0.3)
    except Exception:
        pass


def get_status():
    if not ROVER_IP:
        return {"blocked": 0, "dist": -1}
    try:
        r = requests.get(f"http://{ROVER_IP}/status", timeout=0.3)
        return r.json()
    except Exception:
        return {"blocked": 0, "dist": -1}


def angle_to_target(tx, ty):
    dx = tx - pos_x
    dy = ty - pos_y
    return math.degrees(math.atan2(dy, dx))


def distance_to_target(tx, ty):
    return math.sqrt((tx - pos_x) ** 2 + (ty - pos_y) ** 2)


def angle_diff(target_angle, current_angle):
    return (target_angle - current_angle + 180) % 360 - 180


def find_passenger(frame):
    results = model(frame, verbose=False)
    boxes = results[0].boxes
    frame_h, frame_w = frame.shape[:2]

    best = None
    best_area = 0

    for box in boxes:
        cls = int(box.cls[0])
        label = model.names[cls]
        conf = float(box.conf[0])
        if label != "person" or conf < 0.4:
            continue
        x1, y1, x2, y2 = box.xyxy[0].tolist()
        area = (x2 - x1) * (y2 - y1)
        if area > best_area:
            best_area = area
            best = {
                "x1": x1, "y1": y1, "x2": x2, "y2": y2,
                "width_ratio": (x2 - x1) / frame_w,
                "height_ratio": (y2 - y1) / frame_h,
                "conf": conf,
            }

    return best, results[0]


# ---- Gate selection ----
print("=== Airport Gate Guide ===")
print(f"Available gates: {', '.join(sorted(GATES.keys()))}")
gate = input("Enter gate number: ").strip().upper()

if gate not in GATES:
    raise SystemExit(f"Unknown gate: {gate}")

waypoints = GATES[gate]
print(f"\nGuiding to Gate {gate} ({len(waypoints)} waypoints)")
if not ROVER_IP:
    print("(Dry run — no rover connected)")
print("Press Esc to cancel\n")

send_face("happy")
time.sleep(1)
send_face("idle")

state = "MOVING"

while current_wp < len(waypoints):
    ok, frame = cap.read()
    if not ok:
        continue

    passenger, yolo_result = find_passenger(frame)
    annotated = yolo_result.plot()

    rover_status = get_status()
    ultrasonic_blocked = rover_status.get("blocked", 0)

    target_x, target_y = waypoints[current_wp]
    dist = distance_to_target(target_x, target_y)
    target_angle = angle_to_target(target_x, target_y)
    turn = angle_diff(target_angle, heading)

    if passenger:
        last_person_seen = time.time()

    passenger_lost = (time.time() - last_person_seen) > LOST_TIMEOUT
    passenger_too_far = passenger and passenger["height_ratio"] < FOLLOW_DISTANCE_MIN
    passenger_too_close = passenger and passenger["height_ratio"] > FOLLOW_DISTANCE_MAX

    if state == "MOVING":
        if ultrasonic_blocked:
            state = "AVOIDING"
            send_cmd("stop")
            send_face("alert")
            print("  Obstacle (ultrasonic)! Avoiding...")
        elif passenger_lost or passenger_too_far:
            state = "WAITING"
            send_cmd("stop")
            send_face("idle")
            if passenger_lost:
                print("  Passenger lost — waiting...")
            else:
                print("  Passenger too far — waiting...")
        elif dist < ARRIVAL_THRESHOLD:
            send_cmd("stop")
            print(f"  Reached waypoint {current_wp + 1}/{len(waypoints)}")
            current_wp += 1
            time.sleep(0.3)
        elif abs(turn) > 20:
            cmd = "left" if turn > 0 else "right"
            send_cmd(cmd, ms=TURN_MS, speed=TURN_SPEED)
            time.sleep(TURN_MS / 1000.0)
            heading += (15 if turn > 0 else -15)
            heading %= 360
        else:
            send_cmd("forward", ms=MOVE_MS, speed=MOVE_SPEED)
            time.sleep(MOVE_MS / 1000.0)
            pos_x += MOVE_SPEED_CM_S * (MOVE_MS / 1000.0) * math.cos(math.radians(heading))
            pos_y += MOVE_SPEED_CM_S * (MOVE_MS / 1000.0) * math.sin(math.radians(heading))
            print(f"  Forward → pos=({pos_x:.0f}, {pos_y:.0f})")

    elif state == "WAITING":
        if passenger and not passenger_too_far:
            state = "MOVING"
            send_face("happy")
            print("  Passenger is back — resuming")
            time.sleep(0.5)
            send_face("idle")

    elif state == "AVOIDING":
        if not ultrasonic_blocked:
            state = "MOVING"
            send_face("idle")
            print("  Path clear — resuming")
        else:
            send_cmd("right", ms=TURN_MS, speed=TURN_SPEED)
            time.sleep(TURN_MS / 1000.0)
            heading -= 15
            heading %= 360

    # On-screen display
    status_color = {
        "MOVING": (0, 255, 0),
        "WAITING": (0, 255, 255),
        "AVOIDING": (0, 0, 255),
    }
    color = status_color.get(state, (255, 255, 255))

    cv2.putText(annotated, f"Gate {gate} | WP {current_wp + 1}/{len(waypoints)} | {dist:.0f}cm",
                (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, color, 2)
    cv2.putText(annotated, f"State: {state}",
                (10, 60), cv2.FONT_HERSHEY_SIMPLEX, 0.7, color, 2)

    pax = "FOLLOWING" if passenger and not passenger_too_far else ("TOO FAR" if passenger_too_far else "LOST")
    cv2.putText(annotated, f"Passenger: {pax}",
                (10, 90), cv2.FONT_HERSHEY_SIMPLEX, 0.7, color, 2)

    us_dist = rover_status.get("dist", -1)
    cv2.putText(annotated, f"Ultrasonic: {us_dist}mm",
                (10, 120), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (200, 200, 200), 2)

    cv2.imshow("Gate Guide", annotated)
    if cv2.waitKey(1) == 27:
        break

send_cmd("stop")
if current_wp >= len(waypoints):
    send_face("happy")
    print(f"\n=== Arrived at Gate {gate}! Have a safe flight! ===")
else:
    send_face("idle")
    print("\n=== Navigation cancelled ===")

cap.release()
cv2.destroyAllWindows()
