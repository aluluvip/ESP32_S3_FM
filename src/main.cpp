#include "Arduino.h"
#include "WiFi.h"
#include "Audio.h" // 包含音频库ESP32-audioI2S 3.0.0
#include "U8g2lib.h" // 包含U8g2 OLED库
#include "time.h" // 包含时间库用于NTP同步
#include "Wire.h" // 包含硬件 I2C 库

// Digital I/O used - using your existing wiring configuration
#define I2S_DOUT      7
#define I2S_BCLK      15
#define I2S_LRC       16

// Volume control buttons
#define VOLUME_UP_PIN   40  // GPIO40 - Volume up button
#define VOLUME_DOWN_PIN 39  // GPIO39 - Volume down button (short press: decrease volume, long press: mute)

// Mode switch button
#define MODE_BUTTON_PIN  0   // GPIO0 - Mode switch button (press to toggle volume/channel mode)

// OLED configuration - Hardware I2C
#define OLED_SDA 47      // GPIO41 - OLED SDA pin
#define OLED_SCL 21      // GPIO21 - OLED SCL pin
#define SCREEN_WIDTH 128 // OLED screen width
#define SCREEN_HEIGHT 64 // OLED screen height

// Initialize U8g2 library for SSD1306 OLED with Hardware I2C
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, /* reset=*/U8X8_PIN_NONE);

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
    {"亳州交通音乐广播经典音乐", "https://lhttp-hw.qtfm.cn/live/647/64k.mp3"},
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
unsigned long lastNtpSync = 0;
const unsigned long NTP_SYNC_INTERVAL = 3600000;  // Sync time every hour

// WiFi reconnection variables
unsigned long lastWifiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 5000;  // Check WiFi every 5 seconds

// Scrolling text variables
int scrollOffset = 0;

// Mode switch variables
bool volumeMode = true;  // true = 音量模式，false = 频道切换模式

// Button timing constants
const unsigned long LONG_PRESS_THRESHOLD = 800;  // 800ms for long press detection
const unsigned long DEBOUNCE_DELAY = 50;         // 50ms debounce delay

// Function declarations
void updateDisplay();
void checkVolumeButtons();
void checkModeButton();
void switchStream(int direction);

// FreeRTOS task for display updates
void displayUpdateTask(void *pvParameters);

// FreeRTOS task implementation for display updates
void displayUpdateTask(void *pvParameters) {
    static int lastSecond = -1;

    while (true) {
        unsigned long currentMillis = millis();

        // Check WiFi connection and reconnect if needed
        if (currentMillis - lastWifiCheck > WIFI_CHECK_INTERVAL) {
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("WiFi disconnected! Reconnecting...");
                WiFi.reconnect();
            }
            lastWifiCheck = currentMillis;
        }

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

        vTaskDelay(100 / portTICK_PERIOD_MS); // Delay 100ms to reduce CPU usage
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
    
    // Connect to WiFi with timeout
    Serial.println("Connecting to WiFi...");
    WiFi.begin(ssid.c_str(), password.c_str());
    
    unsigned long wifiStart = millis();
    const unsigned long WIFI_TIMEOUT = 10000; // 10 seconds timeout
    
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < WIFI_TIMEOUT) {
        delay(500);
        Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("");
        Serial.println("WiFi connected");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("");
        Serial.println("WiFi connection failed! Will retry in background...");
    }
    
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
    
    // Initialize OLED display with Hardware I2C
    Wire.begin(OLED_SDA, OLED_SCL);
    display.begin();
    display.enableUTF8Print(); // Enable UTF-8 support for Chinese characters
    display.setContrast(255); // Set maximum contrast
    
    // Initialize volume control buttons
    pinMode(VOLUME_UP_PIN, INPUT_PULLUP);
    pinMode(VOLUME_DOWN_PIN, INPUT_PULLUP);
    pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
    
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

    // ========== 外边框 (1px) ==========
    display.drawFrame(1, 1, 126, 62);

    // ========== 顶部状态栏 ==========

    // WiFi 状态 (左侧)
    display.setFont(u8g2_font_open_iconic_www_1x_t);
    display.setCursor(4, 11);
    if (WiFi.status() == WL_CONNECTED) {
        display.print((char)0x48); // WiFi 图标
    } else {
        display.print((char)0x42); // 断开连接图标
    }

    // 模式指示器（WiFi 图标后）
    display.setFont(u8g2_font_6x10_tf);
    if (volumeMode) {
        display.setCursor(16, 11);
        display.print("VOL"); // 音量模式
    } else {
        display.setCursor(16, 11);
        display.print("CH");  // 频道模式
    }

    // 频道信息 (中央)
    display.setFont(u8g2_font_6x10_tf);
    char channelStr[8];
    sprintf(channelStr, "CH.%02d", currentStreamIndex + 1);
    int channelWidth = display.getUTF8Width(channelStr);
    display.setCursor((128 - channelWidth) / 2, 11);
    display.print(channelStr);

    // 音量数值 (右侧)
    display.setFont(u8g2_font_6x10_tf);
    char volumeStr[4];
    sprintf(volumeStr, "%2d", currentVolume);
    int volumeWidth = display.getUTF8Width(volumeStr);
    display.setCursor(128 - volumeWidth - 3, 11);
    display.print(volumeStr);

    // 顶栏分割线
    display.drawHLine(1, 14, 126);

    // ========== 电台名称区域 (中央) ==========
    display.setFont(u8g2_font_wqy12_t_gb2312);
    const char* stationName = audioStreams[currentStreamIndex].name;
    int nameWidth = display.getUTF8Width(stationName);

    // 边距设置（左右各留 4px）
    const int sideMargin = 4;
    const int textGap = 10; // 两个文本之间的间隙
    int availableWidth = 128 - sideMargin * 2; // 120px

    // 判断是否需要滚动
    bool needScroll = (nameWidth > availableWidth);

    // 需要滚动时，每次调用更新位置（每秒更新一次）
    if (needScroll) {
        scrollOffset += 2; // 每次滚动 2px
        // 循环滚动：当文本完全滚出左边界后重置
        // 总滚动距离 = 文字宽度 + 间隙 + 屏幕可用宽度
        int totalScroll = nameWidth + textGap + availableWidth;
        if (scrollOffset >= totalScroll) {
            scrollOffset = 0;
        }
    }

    // 绘制电台名称（往下移动 4px）
    int yPos = 34;

    if (!needScroll) {
        // 短名称居中显示
        display.setCursor((128 - nameWidth) / 2, yPos);
        display.print(stationName);
    } else {
        // 长名称滚动显示：从右向左无缝循环滚动
        // 循环周期 = 文字宽度 + 间隙
        int cycleWidth = nameWidth + textGap;
        
        // 计算当前在循环中的位置
        int posInCycle = scrollOffset % cycleWidth;
        
        // 文本 1 起始位置：从右侧进入
        // posInCycle=0 时，文本**右边缘**在 x=124，即左边缘在 x=124-nameWidth
        int xPos1 = (128 - sideMargin - nameWidth) - posInCycle;
        
        // 设置裁剪区域，确保文本不会超出屏幕
        display.setClipWindow(sideMargin, 0, 128 - sideMargin, 64);
        
        // 绘制文本 1
        display.setCursor(xPos1, yPos);
        display.print(stationName);
        
        // 绘制文本 2（在文本 1 右边，形成循环）
        int xPos2 = xPos1 + cycleWidth;
        // 只有当文本 2 部分在屏幕内时才绘制
        if (xPos2 < 128 - sideMargin) {
            display.setCursor(xPos2, yPos);
            display.print(stationName);
        }
        
        // 恢复全屏显示
        display.setMaxClipWindow();
    }

    // ========== 底部信息栏 ==========
    // 绘制分隔线
    display.drawHLine(1, 46, 126);
    
    // 星期几 (左侧) - 英文显示，与时间字体一致
    display.setFont(u8g2_font_6x10_tf);
    const char* weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    if (timeSynced && getLocalTime(&timeinfo)) {
        display.setCursor(4, 58);
        display.print(weekdays[timeinfo.tm_wday]);
    } else {
        display.setCursor(4, 58);
        display.print("     ");
    }

    // 系统时间 (右侧) - 只显示时间，带秒
    char systemTimeStr[9];
    if (timeSynced && getLocalTime(&timeinfo)) {
        strftime(systemTimeStr, sizeof(systemTimeStr), "%H:%M:%S", &timeinfo);
    } else {
        unsigned long systemTime = currentMillis / 1000;
        int sysHours = (systemTime / 3600) % 24;
        int sysMinutes = (systemTime / 60) % 60;
        int sysSeconds = systemTime % 60;
        sprintf(systemTimeStr, "%02d:%02d:%02d", sysHours, sysMinutes, sysSeconds);
    }

    display.setFont(u8g2_font_6x10_tf);
    int systemTimeWidth = display.getUTF8Width(systemTimeStr);
    display.setCursor(128 - systemTimeWidth - 4, 58);
    display.print(systemTimeStr);

    display.sendBuffer();
}

// Function to check mode button and toggle between volume/channel mode
void checkModeButton() {
    static unsigned long lastDebounce = 0;
    static bool lastState = HIGH;
    static bool buttonPressed = false;
    
    int currentState = !digitalRead(MODE_BUTTON_PIN); // Active low
    unsigned long currentMillis = millis();
    
    // Debounce
    if (currentState != lastState) {
        lastDebounce = currentMillis;
        lastState = currentState;
    }
    
    if (currentMillis - lastDebounce > DEBOUNCE_DELAY) {
        if (currentState && !buttonPressed) {
            // Button pressed - toggle mode
            volumeMode = !volumeMode;
            buttonPressed = true;
            
            Serial.println(volumeMode ? "Mode: Volume" : "Mode: Channel");
        } else if (!currentState && buttonPressed) {
            // Button released
            buttonPressed = false;
        }
    }
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
            
            if (volumeMode) {
                // 音量模式：增加音量
                if (currentVolume < 21) {
                    audio.setVolume(++currentVolume);
                    Serial.printf("Volume increased to: %d\n", currentVolume);

                }
            } else {
                // 频道模式：下一个频道
                Serial.println("Next stream");
                switchStream(1);
            }
        } else if (!currentUpState && volumeUpPressed) {
            // Button released
            volumeUpPressed = false;
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
            volumeDownPressStartTime = currentMillis;
            muteTriggered = false;
            
            if (volumeMode) {
                // 音量模式：减小音量
                if (currentVolume > 0) {
                    audio.setVolume(--currentVolume);
                    Serial.printf("Volume decreased to: %d\n", currentVolume);

                }
            } else {
                // 频道模式：上一个频道
                Serial.println("Previous stream");
                switchStream(-1);
            }
        } else if (!currentDownState && volumeDownPressed) {
            // Button released
            volumeDownPressed = false;
        }
    }

    // Check for long press (non-blocking) - only in volume mode
    if (volumeMode && currentDownState && volumeDownPressed && !muteTriggered) {
        if (currentMillis - volumeDownPressStartTime >= LONG_PRESS_THRESHOLD) {
            // Long press: mute (set volume to 0)
            if (currentVolume > 0) {
                audio.setVolume(0);
                Serial.println("Volume muted (long press)");
                muteTriggered = true;
            }
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

    // Reset scroll offset when switching stations
    scrollOffset = 0;

    // Display will be updated by FreeRTOS task automatically
    Serial.println("Stream switched!");
}



void loop(){
    audio.loop();
    checkModeButton();
    checkVolumeButtons();
}