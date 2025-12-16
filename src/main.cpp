#include "Arduino.h"
#include "WiFi.h"
#include "Audio.h" // 包含音频库ESP32-audioI2S 3.0.0
#include "U8g2lib.h" // 包含U8g2 OLED库
#include "time.h" // 包含时间库用于NTP同步

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

// Audio stream structure
struct AudioStream {
    const char* name;
    const char* url;
};

// Audio stream URLs with names
AudioStream audioStreams[] = {
    {"经典音乐", "https://lhttp-hw.qtfm.cn/live/647/64k.mp3"},
    {"新闻广播", "https://lhttp-hw.qtfm.cn/live/15318194/64k.mp3"},
    {"流行音乐", "https://lhttp-hw.qtfm.cn/live/20500104/64k.mp3"},
    {"亳州交通音乐广播FM107.2", "https://lhttp-hw.qtfm.cn/live/20212419/64k.mp3"},
    {"摇滚音乐", "https://lhttp-hw.qtfm.cn/live/5021902/64k.mp3"}
};
const int NUM_STREAMS = sizeof(audioStreams) / sizeof(audioStreams[0]);
int currentStreamIndex = 0;

// Removed volume display variables - no longer need fullscreen volume display

// NTP time synchronization variables
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 8 * 3600;  // UTC+8 for China
const int daylightOffset_sec = 0;  // No daylight saving time in China
struct tm timeinfo;
bool timeSynced = false;

// Playback and time variables
unsigned long playbackStartTime = 0;
unsigned long lastTimeUpdate = 0;
const unsigned long TIME_UPDATE_INTERVAL = 1000;  // Update time every 1 second
unsigned long lastNtpSync = 0;
const unsigned long NTP_SYNC_INTERVAL = 3600000;  // Sync time every hour

// Scrolling text variables
unsigned long lastScrollUpdate = 0;
const unsigned long SCROLL_UPDATE_INTERVAL = 200;  // Scroll speed
int scrollOffset = 0;
int maxScrollOffset = 0;

// Button timing constants
const unsigned long LONG_PRESS_THRESHOLD = 800;  // 800ms for long press detection
const unsigned long DEBOUNCE_DELAY = 50;         // 50ms debounce delay
const unsigned long DOUBLE_CLICK_THRESHOLD = 300;  // 300ms for double click detection

// Function declarations
void updateDisplay();
void checkVolumeButtons();
void switchStream(int direction);

// FreeRTOS task for display updates
void displayUpdateTask(void *pvParameters);

// FreeRTOS task implementation for display updates
void displayUpdateTask(void *pvParameters) {
    static int lastSecond = -1;
    
    while (true) {
        unsigned long currentMillis = millis();
        
        // Only update when the second changes, once per second
        int currentSecond = (currentMillis / 1000) % 60;
        if (currentSecond != lastSecond) {
            // Update the global timeinfo before updating display
            getLocalTime(&timeinfo);
            updateDisplay();
            lastSecond = currentSecond;
        }
        
        // Update NTP time periodically
        if (currentMillis - lastNtpSync > NTP_SYNC_INTERVAL) {
            configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
            if (getLocalTime(&timeinfo)) {
                timeSynced = true;
            }
            lastNtpSync = currentMillis;
        }
        
        vTaskDelay(50 / portTICK_PERIOD_MS); // Delay 50ms to reduce CPU usage
    }
}

void setup() {
    Serial.begin(115200);
    
    // Initialize PSRAM for m3u8 support
    Serial.println("Initializing PSRAM...");
    if (psramInit()) {
        Serial.println("PSRAM initialized successfully!");
    } else {
        Serial.println("PSRAM initialization failed!");
    }
    
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
    
    // Initialize NTP time synchronization
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.println("Initializing NTP time synchronization...");
    
    // Wait a bit for time to sync
    unsigned long startTime = millis();
    while (!getLocalTime(&timeinfo) && millis() - startTime < 5000) {
        delay(100);
        Serial.print(".");
    }
    
    if (getLocalTime(&timeinfo)) {
        Serial.println("NTP time synchronized successfully!");
        timeSynced = true;
        char timeStr[20];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
        Serial.printf("Current time: %s\n", timeStr);
    } else {
        Serial.println("NTP time synchronization failed!");
    }
    
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
    
    // Increase SSL timeout for m3u8 parsing
    audio.setConnectionTimeout(5000, 10000); // Increase SSL timeout to 10 seconds
    
    // Use a reliable HTTP stream URL (HTTPS may cause SSL issues for m3u8)
    Serial.println("Connecting to audio stream...");
    
    // Connect to the first audio stream in the list
    audio.connecttohost(audioStreams[currentStreamIndex].url);
    Serial.printf("Connected to stream %d: %s (%s)\n", currentStreamIndex + 1, audioStreams[currentStreamIndex].name, audioStreams[currentStreamIndex].url);
    
    // Update display to show final status after setup completion
    updateDisplay();
    
    // Create display update task after all initializations
    // This task will run in parallel with the main loop, avoiding audio stutter
    xTaskCreate(
        displayUpdateTask,
        "DisplayUpdateTask",
        2048,        // Stack size
        NULL,         // Parameters
        1,            // Priority (lower than audio)
        NULL          // Task handle
    );
    Serial.println("Display update task created!");

}

// Function to update OLED display with current status
void updateDisplay() {
    display.clearBuffer();
    
    unsigned long currentMillis = millis();
    int currentVolume = audio.getVolume();
    
    // Normal display mode - always show this layout
    // Top bar with WiFi, channel, and volume
    display.setFont(u8g2_font_6x10_tf);
    
    // WiFi status (left)
    // Set iconic font for WiFi icon
    display.setFont(u8g2_font_open_iconic_www_1x_t);
    
    // Print WiFi icon (0x48 is the WiFi icon in open_iconic_www fonts)
    display.setCursor(0, 10);
    display.print((char)0x48);
    
    // Channel info (center)
    // Set back to ASCII font for channel info
    display.setFont(u8g2_font_6x10_tf);
    
    char channelStr[6];
    sprintf(channelStr, "%02d/%02d", currentStreamIndex + 1, NUM_STREAMS);
    int channelWidth = display.getUTF8Width(channelStr);
    display.setCursor((128 - channelWidth) / 2, 10);
    display.print(channelStr);
    
    // Volume info (right)
    // Set iconic font for speaker icon - using U8g2 font instead of U8x8
    display.setFont(u8g2_font_open_iconic_play_1x_t);
    
    // Print speaker icon (0x40 is the speaker icon in open_iconic_play fonts)
    display.setCursor(108, 10); // Position before volume number
    display.print((char)0x50);
    
    // Set back to original font for volume number
    display.setFont(u8g2_font_6x10_tf);
    
    // Print volume number
    char volumeStr[4];
    sprintf(volumeStr, "%02d", currentVolume);
    display.setCursor(117, 10); // Position for volume number
    display.print(volumeStr);
    
    // Horizontal line separator
    display.drawHLine(0, 12, 128);
    
    // Channel name with scrolling (middle section)
    display.setFont(u8g2_font_wqy12_t_gb2312);
    const char* stationName = audioStreams[currentStreamIndex].name;
    int nameWidth = display.getUTF8Width(stationName);
    
    // Calculate max scroll offset if not already done
    if (maxScrollOffset == 0 && nameWidth > 128) {
        maxScrollOffset = nameWidth - 128 + 10; // Add some padding
    }
    
    // Update scroll offset
    if (currentMillis - lastScrollUpdate > SCROLL_UPDATE_INTERVAL && nameWidth > 128) {
        scrollOffset++;
        if (scrollOffset > maxScrollOffset) {
            scrollOffset = 0;
        }
        lastScrollUpdate = currentMillis;
    }
    
    // Draw scrolling text
    int xPos = -scrollOffset;
    display.setCursor(xPos, 32);
    display.print(stationName);
    
    // Draw scrolling text again if it's wrapping
    if (nameWidth > 128 && xPos + nameWidth < 128) {
        display.setCursor(xPos + nameWidth + 10, 32); // Add gap between repetitions
        display.print(stationName);
    }
    
    // 分隔线（底部） - 向下移动
    display.drawHLine(0, 50, 128);
    
    // Bottom bar with playback time and system time - moved closer to bottom
    display.setFont(u8g2_font_6x10_tf);
    
    // Playback time (left) - moved down to y=60
    unsigned long playbackTime = currentMillis - playbackStartTime;
    char playbackTimeStr[10];
    int hours = (playbackTime / 3600000) % 24;
    int minutes = (playbackTime / 60000) % 60;
    int seconds = (playbackTime / 1000) % 60;
    sprintf(playbackTimeStr, "%02d:%02d:%02d", hours, minutes, seconds);
    display.setCursor(0, 60);
    display.print(playbackTimeStr);
    
    // System time (right) - using NTP synced time - moved down to y=60
    char systemTimeStr[9];
    
    if (timeSynced && getLocalTime(&timeinfo)) {
        // Format time as HH:MM:SS
        strftime(systemTimeStr, sizeof(systemTimeStr), "%H:%M:%S", &timeinfo);
    } else {
        // Fallback to millis() if NTP sync failed
        unsigned long systemTime = currentMillis / 1000;
        int sysHours = (systemTime / 3600) % 24;
        int sysMinutes = (systemTime / 60) % 60;
        int sysSeconds = systemTime % 60;
        sprintf(systemTimeStr, "%02d:%02d:%02d", sysHours, sysMinutes, sysSeconds);
    }
    
    int systemTimeWidth = display.getUTF8Width(systemTimeStr);
    display.setCursor(128 - systemTimeWidth, 60);
    display.print(systemTimeStr);
    
    display.sendBuffer();
}

// Function to check volume control buttons and handle volume changes
void checkVolumeButtons() {
    static unsigned long lastVolumeUpDebounce = 0;
    static unsigned long lastVolumeDownDebounce = 0;
    static bool lastUpState = HIGH;
    static bool lastDownState = HIGH;
    static bool volumeUpPressed = false;
    static bool volumeDownPressed = false;
    static unsigned long volumeDownPressStartTime = 0;
    static bool muteTriggered = false;
    
    // Double-click detection variables
    static unsigned long lastVolumeUpClickTime = 0;
    static unsigned long lastVolumeDownClickTime = 0;
    static int volumeUpClickCount = 0;
    static int volumeDownClickCount = 0;
    
    // Volume adjustment delay variables
    static bool volumeUpNeedsAdjust = false;
    static bool volumeDownNeedsAdjust = false;
    static unsigned long volumeUpAdjustTime = 0;
    static unsigned long volumeDownAdjustTime = 0;
    
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
            
            // Increase click count for double-click detection
            volumeUpClickCount++;
            
            if (volumeUpClickCount == 1) {
                // First click, start timer
                lastVolumeUpClickTime = currentMillis;
                volumeUpNeedsAdjust = true;
                volumeUpAdjustTime = currentMillis;
            } else if (volumeUpClickCount == 2) {
                // Double click detected
                Serial.println("Double click up - Next stream");
                switchStream(1); // Switch to next stream
                volumeUpClickCount = 0; // Reset click count
                volumeUpNeedsAdjust = false; // Cancel volume adjustment
            }
        } else if (!currentUpState && volumeUpPressed) {
            // Button released
            volumeUpPressed = false;
        }
    }
    
    // Handle volume up adjustment after debouncing for double-click
    if (volumeUpNeedsAdjust && currentMillis - volumeUpAdjustTime > DOUBLE_CLICK_THRESHOLD) {
        // Single click confirmed, adjust volume
        if (currentVolume < 21) {
            audio.setVolume(++currentVolume);
            Serial.printf("Volume increased to: %d\n", currentVolume);
            updateDisplay();
        }
        volumeUpNeedsAdjust = false;
        volumeUpClickCount = 0;
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
            volumeDownPressStartTime = currentMillis;
            muteTriggered = false;
            
            // Increase click count for double-click detection
            volumeDownClickCount++;
            
            if (volumeDownClickCount == 1) {
                // First click, start timer
                lastVolumeDownClickTime = currentMillis;
                volumeDownNeedsAdjust = true;
                volumeDownAdjustTime = currentMillis;
            } else if (volumeDownClickCount == 2) {
                // Double click detected
                Serial.println("Double click down - Previous stream");
                switchStream(-1); // Switch to previous stream
                volumeDownClickCount = 0; // Reset click count
                volumeDownNeedsAdjust = false; // Cancel volume adjustment
            }
        } else if (!currentDownState && volumeDownPressed) {
            // Button released
            volumeDownPressed = false;
        }
    }
    
    // Handle volume down adjustment after debouncing for double-click
    if (volumeDownNeedsAdjust && currentMillis - volumeDownAdjustTime > DOUBLE_CLICK_THRESHOLD) {
        // Single click confirmed, adjust volume
        if (currentVolume > 0) {
            audio.setVolume(--currentVolume);
            Serial.printf("Volume decreased to: %d\n", currentVolume);
            updateDisplay();
        }
        volumeDownNeedsAdjust = false;
        volumeDownClickCount = 0;
    }
    
    // Check for long press (non-blocking)
    if (currentDownState && volumeDownPressed && !muteTriggered) {
        if (currentMillis - volumeDownPressStartTime >= LONG_PRESS_THRESHOLD) {
            // Long press: mute (set volume to 0)
            if (currentVolume > 0) {
                audio.setVolume(0);
                Serial.println("Volume muted (long press)");
                updateDisplay();
                muteTriggered = true;
            }
            // Cancel volume adjustment and double-click detection
            volumeDownNeedsAdjust = false;
            volumeDownClickCount = 0;
        }
    }
}

// Function to switch between audio streams
void switchStream(int direction) {
    // Stop current audio stream
    audio.stopSong();
    
    // Calculate new stream index
    currentStreamIndex += direction;
    if (currentStreamIndex >= NUM_STREAMS) {
        currentStreamIndex = 0;
    } else if (currentStreamIndex < 0) {
        currentStreamIndex = NUM_STREAMS - 1;
    }
    
    // Connect to new stream
    Serial.printf("Switching to stream %d: %s (%s)\n", currentStreamIndex + 1, audioStreams[currentStreamIndex].name, audioStreams[currentStreamIndex].url);
    audio.connecttohost(audioStreams[currentStreamIndex].url);
    
    // Reset playback start time
    playbackStartTime = millis();
    
    // Reset scrolling variables
    scrollOffset = 0;
    maxScrollOffset = 0;
    
    // Show stream change on display
    updateDisplay();
}



void loop(){
    audio.loop();
    checkVolumeButtons();
}