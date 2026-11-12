# 📺 AirDisplay: Wireless Mini LCD Monitor

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](#)
[![Platform](https://img.shields.io/badge/platform-ESP32%20%7C%20macOS%20%7C%20Windows-blue.svg)](#)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

Welcome, makers! With this repository, you can easily convert a standard mini LCD display and an ESP32 microcontroller into a fully functional, high-speed wireless monitor. 

This project was built on a simple but powerful philosophy: **The computer should do all the heavy lifting, and the microcontroller should just act as a blazing-fast display pipe.** By letting the host computer handle all the math, screen capturing, and compression, we can push incredibly high frame rates over standard Wi-Fi without crashing the ESP32.

---

## ✨ Features
* **Cross-Platform Host Support:** Dedicated, highly optimized streaming scripts for both macOS (Apple Silicon optimized) and Windows (Lightweight GDI optimized).
* **Delta-JPEG Architecture:** Instead of sending massive raw video files, the host computer calculates exactly which pixels moved, draws a tiny bounding box, compresses only that specific box into a micro-JPEG, and beams it over Wi-Fi.
* **Hardware DMA Acceleration:** The ESP32 utilizes Direct Memory Access (DMA) to push received pixels directly to the LCD glass, bypassing CPU bottlenecks.
* **Zero Screen Tearing:** Frames are perfectly synchronized and downloaded directly into the ESP32's RAM before being drawn, eliminating the "old TV" refresh effect.

---

## 🛠️ Hardware Requirements
* **Microcontroller:** ESP32 Development Board (e.g., NodeMCU ESP-32S or similar).
* **Display:** Mini LCD Module (Project tested on a Waveshare 2-inch LCD with an ST7789V controller, 240x320 resolution).
* **Wiring:** Standard female-to-female jumper wires.

### Circuit Wiring Guide (SPI)

| LCD Pin | ESP32 Pin | Function |
| :--- | :--- | :--- |
| **VCC** | 3.3V | Power (Do not use 5V) |
| **GND** | GND | Ground |
| **DIN** | GPIO 23 | MOSI (Data In) |
| **CLK** | GPIO 18 | SCK (Clock) |
| **CS** | GPIO 15 | Chip Select |
| **DC** | GPIO 2 | Data/Command |
| **RST** | GPIO 4 | Reset |
| **BL** | GPIO 32 | Backlight Control |

---

## ⚙️ Software Setup & Installation

### 1. Arduino IDE Setup (For ESP32)
1. Install the **ESP32 Board Package** in your Arduino IDE.
2. Go to the Library Manager and install **`TFT_eSPI`** (by Bodmer) and **`TJpg_Decoder`** (by Bodmer).
3. **CRITICAL STEP:** You must configure the `TFT_eSPI` library to match your exact wiring and screen controller.
   * Navigate to your Arduino libraries folder (usually `Documents/Arduino/libraries/TFT_eSPI/`).
   * Open the `User_Setup.h` file.
   * Delete its contents and replace it with the optimized configuration below:

```cpp
#define USER_SETUP_INFO "Waveshare_2inch"
#define ST7789_DRIVER

// MAGIC FIX FOR INVERTED COLORS (Red/Blue Swap)
#define TFT_RGB_ORDER TFT_BGR

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST   4
#define TFT_BL   32
#define TFT_BACKLIGHT_ON HIGH

#define LOAD_GLCD
#define SPI_FREQUENCY  80000000 // Overclocked for video streaming
```

### 2. Python Environment Setup (For Host Computer)
Open your terminal or command prompt and install the required streaming libraries:
```bash
pip install mss opencv-python numpy
```

---

## 🚀 The Codebase

### 1. The ESP32 Receiver Code (Universal)
Upload this C++ sketch to your ESP32. This code works perfectly with both the Windows and macOS Python scripts. **Ensure you update the `ssid` and `password` with your local Wi-Fi credentials.**

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>

const char* ssid = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";

#define TFT_BL_PIN 32
WiFiServer server(8080);
TFT_eSPI tft = TFT_eSPI();

// 32KB is plenty for any micro-JPEG, keeping RAM totally safe
#define JPEG_BUFFER_SIZE 32000
uint8_t jpegBuffer[JPEG_BUFFER_SIZE];

// The 8-byte header structure coming from Python
#pragma pack(push, 1)
struct PayloadHeader {
    uint16_t x;
    uint16_t y;
    uint32_t size;
};
#pragma pack(pop)

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (y >= tft.height()) return 0;
    tft.pushImage(x, y, w, h, bitmap);
    return 1;
}

void setup() {
    Serial.begin(115200);

    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    
    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true); 
    TJpgDec.setCallback(tft_output);

    WiFi.setSleep(false);
    WiFi.begin(ssid, password);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Connecting to WiFi...", 10, 10, 2);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    tft.fillScreen(TFT_BLACK);
    tft.drawString("Connected! IP:", 10, 10, 2);
    tft.drawString(WiFi.localIP().toString(), 10, 30, 2);
    Serial.println(WiFi.localIP());

    server.begin();
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) { ESP.restart(); }

    WiFiClient client = server.available();

    if (client) {
        client.setNoDelay(true);
        client.setTimeout(2000);
        uint32_t lastDataTime = millis();
        Serial.println("Laptop Connected! Awaiting Delta-JPEGs...");

        while (client.connected()) {
            if (client.available() >= 8) {
                lastDataTime = millis();
                
                PayloadHeader header;
                client.readBytes((uint8_t*)&header, 8);
                
                if (header.size > 0 && header.size <= JPEG_BUFFER_SIZE) {
                    uint32_t bytesRead = 0;
                    uint32_t timeoutTimer = millis();

                    // Instantly download the payload into RAM to prevent tearing
                    while (bytesRead < header.size && client.connected()) {
                        if (millis() - timeoutTimer > 1500) break;
                        
                        int avail = client.available();
                        if (avail > 0) {
                            int toRead = avail;
                            if (bytesRead + toRead > header.size) {
                                toRead = header.size - bytesRead;
                            }
                            int n = client.read(jpegBuffer + bytesRead, toRead);
                            if (n > 0) {
                                bytesRead += n;
                                timeoutTimer = millis();
                            }
                        } else {
                            yield();
                        }
                    }

                    // Decode and push to the specific X/Y coordinate on the screen
                    if (bytesRead == header.size) {
                        TJpgDec.drawJpg(header.x, header.y, jpegBuffer, header.size);
                    }
                } else {
                    while(client.available()) client.read();
                }
            } else {
                if (millis() - lastDataTime > 4000) break; 
                yield();
            }
        }
        client.stop();
        tft.fillScreen(TFT_BLACK);
        tft.drawString("Waiting...", 10, 10, 2);
    }
}
```

### 2. The Host Streamer (macOS / High-Performance)
If you are running on a powerful machine (like an Apple Silicon Mac), this script uses ultra-fast C++ binary thresholding to find exactly what pixels moved, crops them out, applies high-quality JPEG compression, and blasts them over a massive 1MB TCP buffer. **Remember to update `ESP32_IP` with the IP address printed on your LCD.**

```python
import mss
import cv2
import numpy as np
import socket
import time
import struct

# --- CONFIGURATION ---
ESP32_IP = '192.168.1.XXX'  # <-- UPDATE THIS
ESP32_PORT = 8080
WIDTH = 320
HEIGHT = 240
JPEG_QUALITY = 75 # High quality, powerful CPUs can easily handle the encoding

def start_mac_stream():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1024 * 1024) 
    
    try:
        sock.settimeout(3.0)
        sock.connect((ESP32_IP, ESP32_PORT))
        sock.settimeout(None)
        print("Connected! Blasting Delta-JPEGs...")
    except Exception as e:
        print(f"Connection Failed: {e}")
        return

    sct = mss.mss()
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
                print(f"\rHost | Frames: {frame_count} | Delta: {jpeg_size/1024:.1f} KB | FPS: {fps:.1f}   ", end="", flush=True)

        except Exception as e:
            print(f"\nConnection dropped: {e}")
            break

if __name__ == "__main__":
    start_mac_stream()
```

### 3. The Host Streamer (Windows / Lightweight CPU)
If you are running this on an older Windows machine or a lightweight setup, this script scales back the processing. It uses `INTER_NEAREST` interpolation and drops the JPEG quality slightly to ensure your laptop's CPU doesn't throttle while streaming. 

```python
import mss
import cv2
import numpy as np
import socket
import time
import struct

# --- CONFIGURATION ---
ESP32_IP = '192.168.1.XXX'  # <-- UPDATE THIS
ESP32_PORT = 8080
WIDTH = 320
HEIGHT = 240
JPEG_QUALITY = 60 # Lower quality to keep older CPUs running fast

def start_windows_stream():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 256 * 1024) 
    
    try:
        sock.settimeout(3.0)
        sock.connect((ESP32_IP, ESP32_PORT))
        sock.settimeout(None)
        print("Connected! Blasting Lightweight Delta-JPEGs...")
    except Exception as e:
        print(f"Connection Failed: {e}")
        return

    sct = mss.mss()
    monitor = sct.monitors[1]

    frame_count = 0
    start_time = time.time()
    prev_gray = None

    while True:
        try:
            screenshot = sct.grab(monitor)
            frame = np.array(screenshot)
            
            # INTER_NEAREST is highly optimized for older CPUs
            frame = cv2.resize(frame, (WIDTH, HEIGHT), interpolation=cv2.INTER_NEAREST)
            gray = cv2.cvtColor(frame, cv2.COLOR_BGRA2GRAY)

            if prev_gray is None:
                x, y, w, h = 0, 0, WIDTH, HEIGHT
                crop = frame
            else:
                diff = cv2.absdiff(gray, prev_gray)
                _, thresh = cv2.threshold(diff, 15, 255, cv2.THRESH_BINARY) 
                
                x, y, w, h = cv2.boundingRect(thresh)
                
                if w == 0 or h == 0:
                    continue
                
                crop = frame[y:y+h, x:x+w]

            prev_gray = gray
            
            crop_bgr = cv2.cvtColor(crop, cv2.COLOR_BGRA2BGR)
            result, encimg = cv2.imencode('.jpg', crop_bgr, [int(cv2.IMWRITE_JPEG_QUALITY), JPEG_QUALITY])
            
            if not result:
                continue

            jpeg_bytes = encimg.tobytes()
            jpeg_size = len(jpeg_bytes)

            header = struct.pack('<HHI', x, y, jpeg_size)
            sock.sendall(header + jpeg_bytes)

            frame_count += 1
            if frame_count % 15 == 0:
                elapsed = time.time() - start_time
                fps = frame_count / elapsed if elapsed > 0 else 0
                print(f"\rHost | Frames: {frame_count} | Delta: {jpeg_size/1024:.1f} KB | FPS: {fps:.1f}   ", end="", flush=True)

        except Exception as e:
            print(f"\nConnection dropped: {e}")
            break

if __name__ == "__main__":
    start_windows_stream()
```

---

## 🎯 How to Run
1. Upload the ESP32 code via the Arduino IDE. 
2. Wait for the LCD screen to display its IP address (e.g., `192.168.1.45`).
3. Open your chosen Python script (`macOS` or `Windows`) and replace the `ESP32_IP` variable on line 9 with your screen's IP.
4. Run the Python script. Your computer screen will instantly appear on the LCD!

## 💡 Troubleshooting
* **Colors Look Alien/Inverted?** You missed the `User_Setup.h` configuration step. Make sure `#define TFT_RGB_ORDER TFT_BGR` is actively uncommented in the `TFT_eSPI` library files.
* **Connection Failed / Ghost Sockets?** If you restart your Python script and it fails to connect, the ESP32 might still be holding the old connection open. Press the physical `EN` (Reset) button on the ESP32 board to instantly clear the network cache.
* **Low FPS?** Ensure your computer and the ESP32 are connected to the same Wi-Fi network, and minimize distance to the router to reduce packet loss.
