import mss
import cv2
import numpy as np
import socket
import time
import struct

# --- MACBOOK CONFIGURATION ---
ESP32_IP = '192.168.29.78'  # <-- UPDATE THIS
ESP32_PORT = 8080
WIDTH = 320
HEIGHT = 240
JPEG_QUALITY = 75 # High quality, Mac can easily handle the encoding

def start_mac_stream():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1024 * 1024) 
    
    try:
        sock.settimeout(3.0)
        sock.connect((ESP32_IP, ESP32_PORT))
        sock.settimeout(None)
        print("MacBook Connected! Blasting Delta-JPEGs...")
    except Exception as e:
        print(f"Connection Failed: {e}")
        return

    sct = mss.MSS()
    monitor = sct.monitors[1]

    frame_count = 0
    start_time = time.time()
    prev_gray = None

    while True:
        try:
            screenshot = sct.grab(monitor)
            frame = np.array(screenshot)
            frame = cv2.resize(frame, (WIDTH, HEIGHT), interpolation=cv2.INTER_LINEAR)
            
            # Grayscale math for extreme CPU efficiency
            gray = cv2.cvtColor(frame, cv2.COLOR_BGRA2GRAY)

            if prev_gray is None:
                x, y, w, h = 0, 0, WIDTH, HEIGHT
                crop = frame
            else:
                diff = cv2.absdiff(gray, prev_gray)
                _, thresh = cv2.threshold(diff, 10, 255, cv2.THRESH_BINARY)
                
                # C-optimized bounding box detection
                x, y, w, h = cv2.boundingRect(thresh)
                
                if w == 0 or h == 0:
                    continue
                
                crop = frame[y:y+h, x:x+w]

            prev_gray = gray
            
            # Format color and compress only the changed box
            crop_bgr = cv2.cvtColor(crop, cv2.COLOR_BGRA2BGR)
            result, encimg = cv2.imencode('.jpg', crop_bgr, [int(cv2.IMWRITE_JPEG_QUALITY), JPEG_QUALITY])
            
            if not result:
                continue

            jpeg_bytes = encimg.tobytes()
            jpeg_size = len(jpeg_bytes)

            # Pack 8-byte Header: X (uint16), Y (uint16), Size (uint32)
            header = struct.pack('<HHI', x, y, jpeg_size)
            sock.sendall(header + jpeg_bytes)

            frame_count += 1
            if frame_count % 30 == 0:
                elapsed = time.time() - start_time
                fps = frame_count / elapsed if elapsed > 0 else 0
                print(f"\rMacBook | Frames: {frame_count} | Delta: {jpeg_size/1024:.1f} KB | FPS: {fps:.1f}   ", end="", flush=True)

        except Exception as e:
            print(f"\nConnection dropped: {e}")
            break

if __name__ == "__main__":
    start_mac_stream()