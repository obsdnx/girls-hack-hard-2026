import cv2

INDEX = 0  # try 0 for iPhone Continuity Camera

cap = cv2.VideoCapture(INDEX, cv2.CAP_AVFOUNDATION)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

if not cap.isOpened():
    raise SystemExit(f"Cannot open camera at index {INDEX}")

print("Stream running — press Esc to quit")

while True:
    ok, frame = cap.read()
    if not ok:
        continue  # skip dropped frames instead of quitting
    cv2.imshow("cam", frame)
    if cv2.waitKey(1) == 27:
        break

cap.release()
cv2.destroyAllWindows()
