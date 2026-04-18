## Cody（ESP32‑C3 屏幕控制系统）

该项目由两部分组成：

- **设备端固件**：`sketch_apr15a/`（ESP32‑C3 + ST7789 240×240 屏幕）
- **PC 串口控制台**：`index.html`（浏览器 WebSerial，与设备端 JSON 串口协议通信）

---

## 目录结构（按功能分组）

### 设备端（`sketch_apr15a/`）

- **入口**
  - `sketch_apr15a/sketch_apr15a.ino`：全局状态 + `setup()` / `loop()` + HTTP 路由注册
  - `sketch_apr15a/partitions.csv`：自定义分区表（用于 `min_spiffs` 场景下扩大 APP）

- **公共头文件**
  - `sketch_apr15a/include/globals.h`：全局对象/状态的 `extern` 声明、依赖库统一引入、引脚宏
  - `sketch_apr15a/include/version.h`：`CURRENT_VERSION`
  - `sketch_apr15a/include/util_hex.h`：HEX 编解码小工具（上传/串口传输用）

- **核心配置**
  - `sketch_apr15a/include/core/config_store.h`
  - `sketch_apr15a/src/core/config_store.cpp`
  - 功能：保存/读取配置（当前图片槽位、轮播间隔、显示模式、置顶笔记等）、清理临时上传文件、打印 FS 空间

- **渲染与显示**
  - `sketch_apr15a/include/render/display_render.h`
  - `sketch_apr15a/src/render/display_render.cpp`
  - 功能：时钟界面、笔记排版（UTF‑8 自动换行）、按模式刷新屏幕
  - `sketch_apr15a/include/render/expression_mode.h`
  - `sketch_apr15a/src/render/expression_mode.cpp`
  - 功能：表情模式（非阻塞状态机动画）、中文提示使用 U8g2 字体渲染

- **存储（LittleFS）**
  - `sketch_apr15a/include/storage/image_store.h`
  - `sketch_apr15a/src/storage/image_store.cpp`
  - 功能：图片槽位扫描、从 LittleFS 读取 RGB565 并显示、轮播下一张切换逻辑

  - `sketch_apr15a/include/storage/note_store.h`
  - `sketch_apr15a/src/storage/note_store.cpp`
  - 功能：笔记的 HTTP API（增删改查、置顶、轮播配置持久化触发刷新）

- **网络/HTTP**
  - `sketch_apr15a/include/net/http_handlers.h`
  - `sketch_apr15a/src/net/http_handlers.cpp`
  - 功能：图片上传（分块）、删除、轮播配置、亮度、模式切换、FS 空间查询等

  - `sketch_apr15a/include/net/system_ops.h`
  - `sketch_apr15a/src/net/system_ops.cpp`
  - 功能：配网失败提示 UI（WiFiManager 回调）、恢复出厂（清除配网并重启）

  - `sketch_apr15a/include/net/ota_update.h`
  - `sketch_apr15a/src/net/ota_update.cpp`
  - 功能：OTA 信息查询、固件下载升级、进度条显示

- **串口协议**
  - `sketch_apr15a/include/protocol/serial_protocol.h`
  - `sketch_apr15a/src/protocol/serial_protocol.cpp`
  - 功能：JSON 命令解析与回包（PC 控制台依赖）

### PC 端（`index.html`）

- 通过 **WebSerial** 打开设备串口（115200）
- 与设备端通过 **一行一条 JSON（以 `\\n` 结尾）** 的协议通信
- 包含：模式切换、图床同步/上传、笔记管理、亮度、WiFi 切换、OTA 触发、FS 空间展示等

---

## 设备端功能清单

- **四种显示模式**
  - **图片模式**：最多 4 个槽位，支持轮播/手动切换、支持设置轮播间隔
  - **时钟模式**：NTP 校时后显示时间/日期/星期
  - **笔记模式**：从 LittleFS 读取笔记，支持置顶与轮播
  - **表情模式**：多种可爱动作随机播放（非阻塞），提示文字使用 U8g2 中文字体避免乱码

- **模式切换过渡**
  - 模式切换时采用背光淡出/淡入，减少闪屏

- **Web 控制面板（设备端 HTTP Server）**
  - 内置页面（固件中 PROGMEM）
  - 图片上传/替换/删除、笔记增删改、亮度调节、OTA、格式化、恢复出厂等

- **LittleFS 存储**
  - `/img{0..3}.bin`：240×240 RGB565（115200 字节）
  - `/notes.json`：笔记数组（带时间）
  - `/config.txt`：运行配置
  - `/tmp_upload.bin`：上传临时文件（完成后 rename）

- **OTA 升级**
  - 读取版本文件、下载固件并刷写
  - 屏幕显示进度条与提示文案

- **WiFi 配网与回退保护**
  - 普通启动：尝试连接历史 WiFi，失败则进入 WiFiManager 配网热点
  - 串口设置 WiFi：尝试新网络失败会回退旧网络（并校验目标 SSID，避免“假连接”）

---

## 串口协议（核心约定）

- **请求**：PC → 设备，`JSON + \\n`
- **响应**：设备 → PC，单行 JSON（包含 `cmd` 与 `status`）

常用命令示例：

```json
{"cmd":"wifi_status"}
```

```json
{"cmd":"get_mode"}
```

```json
{"cmd":"set_mode","mode":1}
```

---

## 编译与烧录（Arduino CLI / ESP32‑C3）

已在本项目中验证的板卡参数（与 IDE 截图一致）：

- FQBN 基础：`esp32:esp32:esp32c3`
- 分区：`PartitionScheme=min_spiffs`（Minimal SPIFFS，1.9MB APP）
- 串口：通常为 `COM5`（以 `board list` 输出为准）

### 编译

```powershell
& "C:\Program Files\Arduino CLI\arduino-cli.exe" compile `
  --fqbn "esp32:esp32:esp32c3:CDCOnBoot=cdc,CPUFreq=160,FlashFreq=80,FlashMode=dio,FlashSize=4M,JTAGAdapter=default,PartitionScheme=min_spiffs,UploadSpeed=921600,EraseFlash=none,ZigbeeMode=default,DebugLevel=none" `
  "d:\mochi\Cody\sketch_apr15a"
```

### 烧录

```powershell
& "C:\Program Files\Arduino CLI\arduino-cli.exe" upload `
  -p COM5 `
  --fqbn "esp32:esp32:esp32c3:CDCOnBoot=cdc,CPUFreq=160,FlashFreq=80,FlashMode=dio,FlashSize=4M,JTAGAdapter=default,PartitionScheme=min_spiffs,UploadSpeed=921600,EraseFlash=none,ZigbeeMode=default,DebugLevel=none" `
  "d:\mochi\Cody\sketch_apr15a"
```

> 提示：烧录时若提示 `port is busy`，请关闭浏览器 WebSerial/串口监视器/串口助手等占用 COM 口的软件。

---

## 表情模式动作清单（当前随机池）

基础表情（0~15）：

- 0 眨眼
- 1 开心
- 2 可怜巴巴
- 3 左右张望
- 4 右眨眼
- 5 晕眩
- 6 左眨眼
- 7 害羞侧看
- 8 思考
- 9 注意到/震惊
- 10 困了
- 11 困惑
- 12 兴奋抖动
- 13 傲娇/不屑
- 14 左右歪头
- 15 睡觉（10 分钟冷却）

道具/互动（16~30）：

- 16 唱歌
- 17 吹风车
- 18 送花
- 20 看书学习
- 21 吃吃吃
- 22 打游戏
- 23 健身举铁
- 24 拍照
- 26 咖啡放松
- 27 绘画
- 29 派对

新增可爱动作（31~40）：

- 32 大笑
- 33 生气
- 34 呜呜哭
- 35 眼里有光
- 37 给你点赞
- 38 夸张震惊
- 39 眼罩睡（省电模式）

已从随机池移除（仍可能保留代码但不会随机触发）：

- 19 吹泡泡
- 25 魔法
- 28 钓鱼
- 30 音乐律动
- 31 爱心暴击
- 36 挥手打招呼
- 40 爱心雨

