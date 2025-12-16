# ESP32-FM

一个基于ESP32-S3开发板的网络FM收音机项目，支持网络音频流播放和硬件按钮音量控制。

## 已实现功能

### 核心功能
- ✅ 网络音频流播放（支持HTTP/HTTPS）
- ✅ 基于ESP32-audioI2S库的高质量音频输出
- ✅ I2S音频接口支持

### 音量控制
- ✅ 硬件按钮音量调节
  - **GPIO40**：音量加按钮（短按+1级音量）
  - **GPIO39**：音量减按钮
    - 短按：-1级音量
    - 长按（>800ms）：立即静音
  - 音量范围：0-21级
  - 串口实时输出音量变化

### 网络功能
- ✅ WiFi连接支持
- ✅ 可配置的WiFi凭据
- ✅ 开机自动连接WiFi

### 调试功能
- ✅ 115200波特率串口调试输出
- ✅ WiFi连接状态显示
- ✅ 音量变化日志

## 硬件连接

### 音频输出
- **I2S_DOUT (GPIO7)**：音频数据输出
- **I2S_BCLK (GPIO15)**：位时钟
- **I2S_LRC (GPIO16)**：左右声道时钟

### 音量控制按钮
- **GPIO40**：音量加按钮（一端接GPIO40，另一端接地）
- **GPIO39**：音量减按钮（一端接GPIO39，另一端接地）
- 按钮采用**INPUT_PULLUP**模式，无需外部上拉电阻

## 软件配置

### 主要配置文件

#### platformio.ini
```ini
[env:esp32-s3-devkitc-1]
platform = espressif32 @ 6.10.0
board = esp32-s3-devkitc-1
framework = arduino
build_flags = 
    -std=gnu++17
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DCORE_DEBUG_LEVEL=3
lib_deps = 
    U8g2 @ 2.36.15
    ESP32-audioI2S @ ^1.9.7
monitor_speed = 115200
```

#### main.cpp中的WiFi配置
```cpp
String ssid =     "your_wifi_ssid";
String password = "your_wifi_password";
```

### 依赖库

- **U8g2** (v2.36.15)：用于OLED显示（预留）
- **ESP32-audioI2S** (v1.9.7+)：用于音频解码和播放
- **WiFi**：ESP32内置WiFi库

## 使用说明

1. **硬件准备**：
   - 连接I2S音频输出设备
   - 连接音量控制按钮
   - 确保ESP32-S3开发板供电正常

2. **软件配置**：
   - 修改`main.cpp`中的WiFi凭据
   - 可选：修改默认音量（初始设置为6）
   - 可选：修改音频流URL

3. **编译上传**：
   - 使用PlatformIO编译并上传代码
   - 打开串口监视器（115200波特率）查看调试信息

4. **使用操作**：
   - 设备开机后自动连接WiFi
   - 连接成功后自动播放预设的音频流
   - 使用音量按钮调节音量
   - 长按音量减按钮实现快速静音

## 音频流配置

当前支持的音频流格式：
- MP3流
- 支持HTTP和HTTPS协议

示例音频流URL：
```cpp
audio.connecttohost("https://lhttp-hw.qtfm.cn/live/647/64k.mp3");
// audio.connecttohost("https://rscdn.ajmide.com/r_69/69.m3u8");
// audio.connecttohost("http://icecast.omroep.nl/radio2-bb-mp3");
```

## 项目结构

```
ESP32-FM/
├── src/
│   └── main.cpp          # 主程序文件
├── platformio.ini        # PlatformIO配置文件
└── README.md             # 项目说明文档
```

## 开发环境

- **开发板**：ESP32-S3-DevKitC-1
- **IDE**：VS Code + PlatformIO
- **框架**：Arduino
- **编译工具链**：espressif32 @ 6.10.0

## 许可证

MIT License

## 注意事项

1. 确保使用稳定的WiFi网络，避免音频卡顿
2. 音频流URL需支持ESP32-audioI2S库
3. 按钮连接时注意引脚方向，避免短路
4. 长时间运行时注意设备散热

## 更新日志

### v1.0.0
- 初始版本发布
- 实现网络音频流播放
- 添加硬件按钮音量控制
- 支持WiFi连接

## 后续计划

- [ ] 添加OLED显示功能
- [ ] 支持多电台切换
- [ ] 添加RTC实时时钟
- [ ] 支持预设电台列表
- [ ] 添加睡眠定时功能

## 贡献

欢迎提交Issue和Pull Request来改进这个项目！