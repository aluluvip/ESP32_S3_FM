#include "Arduino.h"
#include "WiFi.h"
#include "Audio.h" // 包含音频库ESP32-audioI2S 3.0.0

// Digital I/O used - using your existing wiring configuration
#define I2S_DOUT      7
#define I2S_BCLK      15
#define I2S_LRC       16

// Volume control buttons
#define VOLUME_UP_PIN   40  // GPIO40 - Volume up button
#define VOLUME_DOWN_PIN 39  // GPIO39 - Volume down button (short press: decrease volume, long press: mute)

// Your existing WiFi credentials
String ssid =     "lulu";
String password = "19941024";

Audio audio;

// Button state variables
bool volumeUpPressed = false;
bool volumeDownPressed = false;
unsigned long volumeDownPressStartTime = 0;

// Button timing constants
const unsigned long LONG_PRESS_THRESHOLD = 800;  // 800ms for long press detection
const unsigned long DEBOUNCE_DELAY = 50;         // 50ms debounce delay

void setup() {
    Serial.begin(115200);
    
    // Connect to WiFi
    Serial.println("Connecting to WiFi...");
    WiFi.begin(ssid.c_str(), password.c_str());
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    
    // Initialize volume control buttons
    pinMode(VOLUME_UP_PIN, INPUT_PULLUP);
    pinMode(VOLUME_DOWN_PIN, INPUT_PULLUP);
    
    // 使用您现有的引脚配置设置音频
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(6); // 设置音频音量（0...21）
    
    // Use a reliable HTTP stream URL (HTTPS may cause SSL issues)
    Serial.println("Connecting to audio stream...");
    

    // 音乐链接
    // 示例: audio.connecttohost("http://your-stream-url.mp3");
    audio.connecttohost("https://lhttp-hw.qtfm.cn/live/647/64k.mp3");
    // audio.connecttohost("https://rscdn.ajmide.com/r_69/69.m3u8");
    // audio.connecttohost("http://icecast.omroep.nl/radio2-bb-mp3");

}

// Function to check volume control buttons and handle volume changes
void checkVolumeButtons() {
    int currentVolume = audio.getVolume();
    
    // Check volume up button
    bool currentUpState = !digitalRead(VOLUME_UP_PIN); // Active low
    if (currentUpState && !volumeUpPressed) {
        // Button just pressed
        volumeUpPressed = true;
        delay(DEBOUNCE_DELAY);
        
        // Increase volume (max 21)
        if (currentVolume < 21) {
            currentVolume++;
            audio.setVolume(currentVolume);
            Serial.printf("Volume increased to: %d\n", currentVolume);
        }
    } else if (!currentUpState && volumeUpPressed) {
        // Button released
        volumeUpPressed = false;
        delay(DEBOUNCE_DELAY);
    }
    
    // Check volume down button
    bool currentDownState = !digitalRead(VOLUME_DOWN_PIN); // Active low
    if (currentDownState && !volumeDownPressed) {
        // Button just pressed
        volumeDownPressed = true;
        volumeDownPressStartTime = millis();
        delay(DEBOUNCE_DELAY);
    } else if (!currentDownState && volumeDownPressed) {
        // Button released
        volumeDownPressed = false;
        delay(DEBOUNCE_DELAY);
        
        // Check if it was a short press
        if (millis() - volumeDownPressStartTime < LONG_PRESS_THRESHOLD) {
            // Short press: decrease volume (min 0)
            if (currentVolume > 0) {
                currentVolume--;
                audio.setVolume(currentVolume);
                Serial.printf("Volume decreased to: %d\n", currentVolume);
            }
        }
    } else if (currentDownState && volumeDownPressed) {
        // Button still pressed - check for long press
        if (millis() - volumeDownPressStartTime >= LONG_PRESS_THRESHOLD) {
            // Long press: mute (set volume to 0)
            if (currentVolume > 0) {
                audio.setVolume(0);
                Serial.println("Volume muted (long press)");
                // Prevent repeated mute triggers
                delay(LONG_PRESS_THRESHOLD);
            }
        }
    }
}

void loop(){
    audio.loop();
    checkVolumeButtons();
}