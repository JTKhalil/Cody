## Cody（ESP32-C3 固件）

本目录是 **Cody 设备端固件**（Arduino/ESP32），主工程位于 `sketch_apr15a/`。

### 主要能力

- **显示/UI**：240×240 ST7789（`Adafruit_ST7789` + `U8g2_for_Adafruit_GFX`），支持多种显示模式（图片/时钟/笔记/表情）。
- **存储**：`LittleFS` 保存图片、笔记、配置与 BLE 绑定信息。
- **协议层**：
  - **JSONL 控制协议**（一行一个 JSON，以 `\n` 结尾）。实现见 `sketch_apr15a/src/protocol/serial_protocol.cpp`。
  - **二进制分帧协议**（用于图片/OTA 等大吞吐数据），实现见 `sketch_apr15a/src/ble/*` 与 `sketch_apr15a/include/ble/ble_proto.h`。
- **BLE（微信小程序直连）**：
  - GATT Service/Char UUID 固定为：
    - Service：`0000C0DE-0000-1000-8000-00805F9B34FB`
    - RX（写入）：`0000C0D1-0000-1000-8000-00805F9B34FB`
    - TX（Notify）：`0000C0D2-0000-1000-8000-00805F9B34FB`
  - 广播名形如 `Cody-XXXXXX`（取 BT MAC 后 3 字节），实现见 `sketch_apr15a/src/ble/cody_ble.cpp`。
  - **首次连接需要设备端确认**：固件会进入 `pair_pending` 状态，小程序需先发 `{"cmd":"pair_hello","name":"...","id":"..."}`；用户在设备上按键确认后才会放行其它控制命令。
  - **单设备绑定**：以小程序上报的 `clientId` 为“信任设备”标识（避免手机 BLE 隐私地址变化）。
- **OTA（通过 BLE 推送固件）**：实现见 `sketch_apr15a/src/ble/ble_ota.cpp`（`OTA_BEGIN` / `OTA_CHUNK` / `OTA_FINISH`）。
- **（可选）WiFi/Web 调试**：默认关闭以适配 4MB Flash 的更小分区方案。开关见 `sketch_apr15a/include/feature_flags.h`：`CODY_ENABLE_WIFI_DEBUG`。

### 目录结构

- `sketch_apr15a/`
  - `sketch_apr15a.ino`：主入口（`setup()/loop()`），初始化显示、LittleFS、协议、BLE、（可选）WiFi/Web
  - `src/ble/`：BLE GATT、图片传输、BLE OTA、二进制协议
  - `src/protocol/serial_protocol.cpp`：JSON 命令分发与响应（同时转发到 BLE notify）
  - `include/`：各模块头文件（`globals.h`、`version.h`、`feature_flags.h` 等）
  - `flash.ps1`：Windows 一键编译+烧录脚本（基于 `arduino-cli`）
  - `partitions.csv`：分区表示例（如双 OTA 槽 + spiffs）
- `docs/`：与 WXCody（小程序管理端）配套的设计/计划文档

### 环境准备

- **Arduino CLI**：确保 `arduino-cli` 已安装并在 PATH（或按脚本/命令写死的路径）。
- **ESP32 core**：需要安装 Arduino-ESP32 平台（含 `esp32c3`）。
- **依赖库**：项目使用到（至少）：
  - `Adafruit_GFX`
  - `Adafruit_ST7789`
  - `U8g2_for_Adafruit_GFX`
  - `ArduinoJson`
  - `NimBLE-Arduino`（或等价 NimBLE 包）

> 具体依赖版本以你本机 Arduino 库管理器/arduino-cli 安装的为准；若编译报缺库，按报错提示补齐即可。

### 编译与烧录（Windows）

#### 方式 A：使用脚本（推荐）

在 `sketch_apr15a/` 目录运行：

```powershell
.\flash.ps1
# 或指定串口
.\flash.ps1 -Port COM6
```

脚本会调用 `arduino-cli compile` + `arduino-cli upload`，**板卡与分区等选项以 `flash.ps1` 里的 `$Fqbn` 为唯一准绳**（勿与文档或其它脚本各写一套）。

#### 方式 B：手动使用 arduino-cli（必须与 `flash.ps1` 相同 FQBN）

`flash.ps1` 当前使用的 FQBN 为（整段一行，勿换行）：

```text
esp32:esp32:esp32c3:CDCOnBoot=cdc,CPUFreq=160,FlashFreq=80,FlashMode=dio,FlashSize=4M,JTAGAdapter=default,PartitionScheme=huge_app,UploadSpeed=256000,EraseFlash=none,ZigbeeMode=default,DebugLevel=none
```

1) 查找端口：

```powershell
arduino-cli board list
```

2) 仅编译（路径按你本机仓库位置替换）：

```powershell
$Fqbn = 'esp32:esp32:esp32c3:CDCOnBoot=cdc,CPUFreq=160,FlashFreq=80,FlashMode=dio,FlashSize=4M,JTAGAdapter=default,PartitionScheme=huge_app,UploadSpeed=256000,EraseFlash=none,ZigbeeMode=default,DebugLevel=none'
arduino-cli compile "--fqbn=$Fqbn" "d:\mochi\CodyProject\Cody\sketch_apr15a"
```

3) 烧录（将 `COM5` 换成实际端口）：

```powershell
arduino-cli upload "d:\mochi\CodyProject\Cody\sketch_apr15a" -p COM5 "--fqbn=$Fqbn"
```

### 常用协议（给 WXCody/调试用）

#### JSONL（控制面）

发送（每行一个 JSON，以 `\n` 结尾）：

- `{"cmd":"get_mode"}`
- `{"cmd":"set_mode","mode":0}`（0~3）
- `{"cmd":"image_info"}`
- `{"cmd":"get_notes"}`
- `{"cmd":"fs_space"}`
- `{"cmd":"bright","v":255}`
- `{"cmd":"format_fs"}`
- `{"cmd":"reset_system"}`
- `{"cmd":"ota_info"}`
- `{"cmd":"sync_time","timestamp":<unix_seconds>}`

> 设备端会对每个 `cmd` 回同名响应，例如 `{"cmd":"get_mode","status":"ok","mode":1}`。

#### 二进制分帧（图片/OTA）

二进制帧由 `MAGIC(0xC0,0xDE)` 开头，带 CRC16；小程序端实现见 `WXCody/services/proto_bin.js`（源码同名 `.ts`），设备端实现见 `sketch_apr15a/include/ble/ble_proto.h`。

### 版本号

固件版本定义在 `sketch_apr15a/include/version.h`（`CURRENT_VERSION`）。小程序控制台会读取 `ota_info` 与远端 `version.txt` 对比提示升级。
