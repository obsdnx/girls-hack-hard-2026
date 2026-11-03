import cv2, time
print("Looking for cameras (this takes a few seconds)...")
found = False
for i in range(5):
    cap = cv2.VideoCapture(i)
    if not cap.isOpened():
        print("Camera", i, ": not there")
        continue
    time.sleep(1.5)
    bright = 0
    ok = False
    for _ in range(15):
        ok, frame = cap.read()
        time.sleep(0.05)
    if ok and frame is not None:
        bright = int(frame.mean())
        print("Camera", i, ": WORKS, picture brightness", bright, "(0 = black)")
        if bright > 5:
            found = True
    else:
        print("Camera", i, ": opens but gives no picture")
    cap.release()
if not found:
    print("No camera is giving a real picture yet.")
