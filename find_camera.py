import cv2

for i in range(5):
    cap = cv2.VideoCapture(i)
    if cap.isOpened():
        ok, frame = cap.read()
        if ok:
            h, w = frame.shape[:2]
            print(f"Index {i}: WORKS ({w}x{h})")
        else:
            print(f"Index {i}: opens but can't read frames")
        cap.release()
    else:
        print(f"Index {i}: not available")
