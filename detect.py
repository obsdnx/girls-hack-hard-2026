import cv2
import requests
from ultralytics import YOLO

INDEX = 0  # iPhone Continuity Camera
ROVER_IP = ""  # fill in after flashing the ESP32 (e.g. "192.168.1.42")

model = YOLO("yolo11n.pt")

cap = cv2.VideoCapture(INDEX, cv2.CAP_AVFOUNDATION)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

if not cap.isOpened():
    raise SystemExit(f"Cannot open camera at index {INDEX}")

def send_cmd(cmd):
    if not ROVER_IP:
        return
    try:
        requests.get(f"http://{ROVER_IP}/{cmd}", timeout=0.3)
    except Exception:
        pass

print("Detection running — press Esc to quit")
print("Detections will print to terminal")
if not ROVER_IP:
    print("(No ROVER_IP set — detection only, no motor commands)")

last_cmd = "stop"

while True:
    ok, frame = cap.read()
    if not ok:
        continue

    results = model(frame, verbose=False)
    boxes = results[0].boxes
    annotated = results[0].plot()

    frame_h, frame_w = frame.shape[:2]
    center_x = frame_w // 2

    person_found = False
    for box in boxes:
        cls = int(box.cls[0])
        label = model.names[cls]
        conf = float(box.conf[0])
        x1, y1, x2, y2 = box.xyxy[0].tolist()
        obj_center_x = (x1 + x2) / 2
        obj_width = x2 - x1

        if label == "person" and conf > 0.5:
            person_found = True
            if obj_center_x < center_x - 100:
                cmd = "left"
            elif obj_center_x > center_x + 100:
                cmd = "right"
            elif obj_width > frame_w * 0.6:
                cmd = "stop"  # too close
            else:
                cmd = "forward"

            if cmd != last_cmd:
                print(f"Person detected → {cmd} (conf={conf:.2f})")
                send_cmd(cmd)
                last_cmd = cmd
            break

    if not person_found and last_cmd != "stop":
        print("No person → stop")
        send_cmd("stop")
        last_cmd = "stop"

    cv2.imshow("YOLO Detection", annotated)
    if cv2.waitKey(1) == 27:
        break

send_cmd("stop")
cap.release()
cv2.destroyAllWindows()
