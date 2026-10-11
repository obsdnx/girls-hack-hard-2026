import cv2
import requests
import time
import math
from ultralytics import YOLO

# ---- Config ----
INDEX = 0  # camera index
ROVER_IP = ""  # fill in after flashing ESP32

# ---- Waypoints ----
# Define as (x, y) in centimeters from start position
# Rover starts at (0, 0) facing "up" (+y direction)
# Example: go forward 100cm, turn right, go 50cm
WAYPOINTS = [
    (0, 100),    # 1m forward
    (50, 100),   # turn right, go 50cm
    (50, 200),   # turn left, go 1m forward
]

# ---- Navigation state ----
pos_x = 0.0
pos_y = 0.0
heading = 90  # degrees, 90 = facing forward (+y), 0 = facing right (+x)
current_wp = 0
ARRIVAL_THRESHOLD = 15  # cm, "close enough" to waypoint
MOVE_SPEED_CM_S = 20    # estimated rover speed in cm/s — tune this!
TURN_TIME_S = 0.5       # time for a ~45 degree turn — tune this!

# ---- YOLO for obstacle detection ----
model = YOLO("yolo11n.pt")

cap = cv2.VideoCapture(INDEX, cv2.CAP_AVFOUNDATION)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

if not cap.isOpened():
    raise SystemExit(f"Cannot open camera at index {INDEX}")

def send_cmd(cmd):
    if not ROVER_IP:
        print(f"  [would send: {cmd}]")
        return
    try:
        requests.get(f"http://{ROVER_IP}/{cmd}", timeout=0.3)
    except Exception:
        pass

def angle_to_target(tx, ty):
    dx = tx - pos_x
    dy = ty - pos_y
    return math.degrees(math.atan2(dy, dx))

def distance_to_target(tx, ty):
    return math.sqrt((tx - pos_x) ** 2 + (ty - pos_y) ** 2)

def angle_diff(target_angle, current_angle):
    diff = (target_angle - current_angle + 180) % 360 - 180
    return diff

def detect_obstacle(frame):
    results = model(frame, verbose=False)
    boxes = results[0].boxes
    frame_h, frame_w = frame.shape[:2]
    for box in boxes:
        cls = int(box.cls[0])
        label = model.names[cls]
        conf = float(box.conf[0])
        x1, y1, x2, y2 = box.xyxy[0].tolist()
        obj_width = x2 - x1
        obj_height = y2 - y1
        if conf > 0.5 and obj_height > frame_h * 0.5:
            return True, label, (x1 + x2) / 2
    return False, None, None

print("=== Waypoint Navigator ===")
print(f"Waypoints: {WAYPOINTS}")
print(f"Starting at ({pos_x}, {pos_y}), heading {heading}°")
if not ROVER_IP:
    print("(Dry run — no ROVER_IP set, commands print to terminal)")
print()

state = "NAVIGATE"  # NAVIGATE, AVOID, DONE

while current_wp < len(WAYPOINTS):
    ok, frame = cap.read()
    if not ok:
        continue

    target_x, target_y = WAYPOINTS[current_wp]
    dist = distance_to_target(target_x, target_y)
    target_angle = angle_to_target(target_x, target_y)
    turn = angle_diff(target_angle, heading)

    # Check camera for obstacles
    obstacle, obj_label, obj_x = detect_obstacle(frame)
    results = model(frame, verbose=False)
    annotated = results[0].plot()

    # Draw waypoint info
    info = f"WP {current_wp+1}/{len(WAYPOINTS)} | dist={dist:.0f}cm | turn={turn:.0f}deg"
    cv2.putText(annotated, info, (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
    cv2.putText(annotated, f"State: {state}", (10, 60), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)

    if state == "NAVIGATE":
        if obstacle:
            state = "AVOID"
            send_cmd("stop")
            print(f"  Obstacle detected ({obj_label})! Avoiding...")
        elif dist < ARRIVAL_THRESHOLD:
            send_cmd("stop")
            print(f"Reached waypoint {current_wp+1}: ({target_x}, {target_y})")
            current_wp += 1
            time.sleep(0.5)
        elif abs(turn) > 20:
            if turn > 0:
                send_cmd("left")
                print(f"  Turning left ({turn:.0f}°)")
            else:
                send_cmd("right")
                print(f"  Turning right ({turn:.0f}°)")
            time.sleep(TURN_TIME_S)
            heading += (15 if turn > 0 else -15)  # estimate turn amount
            heading = heading % 360
            send_cmd("stop")
        else:
            send_cmd("forward")
            time.sleep(0.3)
            send_cmd("stop")
            # estimate position update
            pos_x += MOVE_SPEED_CM_S * 0.3 * math.cos(math.radians(heading))
            pos_y += MOVE_SPEED_CM_S * 0.3 * math.sin(math.radians(heading))
            print(f"  Forward → pos=({pos_x:.0f}, {pos_y:.0f})")

    elif state == "AVOID":
        if not obstacle:
            state = "NAVIGATE"
            print("  Obstacle cleared, resuming")
        else:
            frame_w = frame.shape[1]
            if obj_x and obj_x < frame_w / 2:
                send_cmd("right")
            else:
                send_cmd("left")
            time.sleep(TURN_TIME_S)
            heading += 15
            heading = heading % 360
            send_cmd("stop")

    cv2.imshow("Rover Navigation", annotated)
    if cv2.waitKey(1) == 27:
        break

send_cmd("stop")
if current_wp >= len(WAYPOINTS):
    print("\n=== Arrived at destination! ===")
else:
    print("\n=== Navigation cancelled ===")

cap.release()
cv2.destroyAllWindows()
