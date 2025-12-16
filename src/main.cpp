#include "Arduino.h"
#include "WiFi.h"
#include "Audio.h" // 包含音频库ESP32-audioI2S 3.0.0
#include "U8g2lib.h" // 包含U8g2 OLED库

// Digital I/O used - using your existing wiring configuration
#define I2S_DOUT      7
#define I2S_BCLK      15
#define I2S_LRC       16

// Volume control buttons
#define VOLUME_UP_PIN   40  // GPIO40 - Volume up button
#define VOLUME_DOWN_PIN 39  // GPIO39 - Volume down button (short press: decrease volume, long press: mute)

// OLED configuration
#define OLED_SDA 41      // GPIO41 - OLED SDA pin
#define OLED_SCL 42      // GPIO42 - OLED SCL pin
#define SCREEN_WIDTH 128 // OLED screen width
#define SCREEN_HEIGHT 64 // OLED screen height

// Initialize U8g2 library for SSD1306 OLED
U8G2_SSD1306_128X64_NONAME_F_SW_I2C display(U8G2_R0, OLED_SCL, OLED_SDA, U8X8_PIN_NONE);

// Your existing WiFi credentials
String ssid =     "lulu";
String password = "19941024";

Audio audio;

// Button state variables
bool volumeUpPressed = false;
bool volumeDownPressed = false;
unsigned long volumeDownPressStartTime = 0;

// Volume display variables
bool showVolumeAdjust = false;
unsigned long volumeAdjustShowTime = 0;
const unsigned long VOLUME_ADJUST_DISPLAY_DURATION = 2000;  // 2 seconds

// Button timing constants
const unsigned long LONG_PRESS_THRESHOLD = 800;  // 800ms for long press detection
const unsigned long DEBOUNCE_DELAY = 50;         // 50ms debounce delay

// Function declarations
void updateDisplay();
void checkVolumeButtons();

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
    
    // Initialize OLED display first
    display.begin();
    display.enableUTF8Print(); // Enable UTF-8 support for Chinese characters
    display.setContrast(255); // Set maximum contrast
    
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
    
    // Update display to show final status after setup completion
    updateDisplay();

}

// Function to update OLED display with current status
void updateDisplay() {
    static bool lastShowVolumeAdjust = false;
    static String lastTitle = "";
    static String lastWiFiStatus = "";
    static String lastIP = "";
    static int lastVolume = -1;
    
    // Only update display if something has changed to reduce CPU usage
    bool needsUpdate = false;
    
    // Check if volume adjustment mode changed
    if (showVolumeAdjust != lastShowVolumeAdjust) {
        needsUpdate = true;
        lastShowVolumeAdjust = showVolumeAdjust;
    }
    
    // Check if display content changed
    if (!showVolumeAdjust) {
        String currentTitle = "ESP32 FM Radio";
        String currentWiFiStatus = "WiFi: 已连接";
        String currentIP = String("IP: ") + WiFi.localIP().toString();
        
        if (currentTitle != lastTitle || currentWiFiStatus != lastWiFiStatus || currentIP != lastIP) {
            needsUpdate = true;
            lastTitle = currentTitle;
            lastWiFiStatus = currentWiFiStatus;
            lastIP = currentIP;
        }
    } else {
        // Volume adjustment mode - check if volume changed
        int currentVolume = audio.getVolume();
        if (currentVolume != lastVolume) {
            needsUpdate = true;
            lastVolume = currentVolume;
        }
    }
    
    // Only update display if needed to reduce CPU usage
    if (needsUpdate) {
        display.clearBuffer();
        
        if (showVolumeAdjust) {
            // Full screen volume adjustment display
            display.setFont(u8g2_font_wqy12_t_gb2312);  // Chinese font (12pt)
            
            // First line: "调节音量" (Centered)
            const char* volumeText = "调节音量";  // Use const char* for UTF-8 compatibility
            int textWidth = display.getUTF8Width(volumeText);
            int xPos = (128 - textWidth) / 2;
            int yPos = 24;  // Lower position for better visibility
            display.setCursor(xPos, yPos);
            display.print(volumeText);
            
            // Second line: Volume level (Centered, two digits)
            int volume = audio.getVolume();
            char volumeStr[3];
            sprintf(volumeStr, "%02d", volume);  // Format as two digits with leading zero
            display.setFont(u8g2_font_logisoso24_tf);  // Even larger font for volume digits
            // Calculate exact width for two digits in this font
            textWidth = display.getUTF8Width(volumeStr);
            xPos = (128 - textWidth) / 2;
            yPos = 60;
            display.setCursor(xPos, yPos);
            display.print(volumeStr);
        } else {
            // Normal display mode - use only one font to avoid font switching overhead
            display.setFont(u8g2_font_6x10_tf);
            
            // Display title - Centered
            String title = "ESP32 FM Radio";
            int titleWidth = display.getUTF8Width(title.c_str());
            int xPos = (128 - titleWidth) / 2;
            display.setCursor(xPos, 10);
            display.print(title);
            
            // Display WiFi status - English for better performance
            display.setCursor(0, 25);
            display.print("WiFi: Connected");
            
            // Display full IP address
            IPAddress ip = WiFi.localIP();
            display.setCursor(0, 40);
            display.printf("IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        }
        
        display.sendBuffer();
    }
}

// Function to check volume control buttons and handle volume changes
void checkVolumeButtons() {
    static unsigned long lastVolumeUpDebounce = 0;
    static unsigned long lastVolumeDownDebounce = 0;
    static bool lastUpState = HIGH;
    static bool lastDownState = HIGH;
    static bool muteTriggered = false;
    
    int currentVolume = audio.getVolume();
    unsigned long currentMillis = millis();
    
    // Check volume up button with debounce
    bool currentUpState = !digitalRead(VOLUME_UP_PIN); // Active low
    if (currentUpState != lastUpState) {
        lastVolumeUpDebounce = currentMillis;
        lastUpState = currentUpState;
    }
    
    if (currentMillis - lastVolumeUpDebounce > DEBOUNCE_DELAY) {
        if (currentUpState && !volumeUpPressed) {
            // Button just pressed
            volumeUpPressed = true;
            
            // Increase volume (max 21)
            if (currentVolume < 21) {
                currentVolume++;
                audio.setVolume(currentVolume);
                Serial.printf("Volume increased to: %d\n", currentVolume);
                showVolumeAdjust = true; // Show volume adjustment screen
                volumeAdjustShowTime = millis(); // Record time
                updateDisplay(); // Update OLED display
            }
        } else if (!currentUpState && volumeUpPressed) {
            // Button released
            volumeUpPressed = false;
            showVolumeAdjust = true; // Keep volume adjustment screen visible
            volumeAdjustShowTime = millis(); // Reset timer when button released
            updateDisplay(); // Update OLED display
        }
    }
    
    // Check volume down button with debounce
    bool currentDownState = !digitalRead(VOLUME_DOWN_PIN); // Active low
    if (currentDownState != lastDownState) {
        lastVolumeDownDebounce = currentMillis;
        lastDownState = currentDownState;
    }
    
    if (currentMillis - lastVolumeDownDebounce > DEBOUNCE_DELAY) {
        if (currentDownState && !volumeDownPressed) {
            // Button just pressed
            volumeDownPressed = true;
            volumeDownPressStartTime = millis();
            muteTriggered = false; // Reset mute trigger flag
        } else if (!currentDownState && volumeDownPressed) {
            // Button released
            volumeDownPressed = false;
            
            // Check if it was a short press
            if (millis() - volumeDownPressStartTime < LONG_PRESS_THRESHOLD) {
                // Short press: decrease volume (min 0)
                if (currentVolume > 0) {
                    currentVolume--;
                    audio.setVolume(currentVolume);
                    Serial.printf("Volume decreased to: %d\n", currentVolume);
                    showVolumeAdjust = true; // Show volume adjustment screen
                    volumeAdjustShowTime = millis(); // Record time
                    updateDisplay(); // Update OLED display
                }
            }
            showVolumeAdjust = true; // Keep volume adjustment screen visible
            volumeAdjustShowTime = millis(); // Reset timer when button released
            updateDisplay(); // Update OLED display
        }
    }
    
    // Check for long press (non-blocking)
    if (currentDownState && volumeDownPressed && !muteTriggered) {
        if (currentMillis - volumeDownPressStartTime >= LONG_PRESS_THRESHOLD) {
            // Long press: mute (set volume to 0)
            if (currentVolume > 0) {
                audio.setVolume(0);
                Serial.println("Volume muted (long press)");
                showVolumeAdjust = true; // Show volume adjustment screen
                volumeAdjustShowTime = millis(); // Record time
                updateDisplay(); // Update OLED display
                muteTriggered = true; // Prevent repeated mute triggers
            }
        }
    }
    
    // Check if volume adjustment screen should be hidden
    if (showVolumeAdjust && millis() - volumeAdjustShowTime >= VOLUME_ADJUST_DISPLAY_DURATION) {
        showVolumeAdjust = false;
        updateDisplay(); // Update OLED display
    }
}

// Variable for display update timing
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_UPDATE_INTERVAL = 1000; // Update display every 1000ms (reduced from 500ms to avoid audio stutter)

void loop(){
    audio.loop();
    checkVolumeButtons();
    // Display update is now only triggered by volume changes, not periodic updates
    // This reduces CPU usage and avoids audio stuttering
}