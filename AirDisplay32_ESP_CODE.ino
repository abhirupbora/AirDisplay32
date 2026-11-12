#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>

const char* ssid = "Your_SSID";
const char* password = "Your_Password";

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