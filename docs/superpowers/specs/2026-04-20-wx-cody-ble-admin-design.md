# WXCody：微信小程序 BLE 管理端（Cody）设计稿

日期：2026-04-20  
项目名：**WXCody**  
目标：基于 Cody 现有「IP Web 控制台 / 串口 JSON 协议」的能力，新增 **微信小程序** 作为管理端，通过 **BLE 蓝牙直连** 控制设备；第一期范围包含 **模式/图库/笔记/设置/OTA**。

---

## 1. 背景与约束

- **只使用蓝牙连接**：不依赖 WiFi/IP Web 管理端，不依赖云端桥接。
- 设备端现状：`sketch_apr15a` 目前以 HTTP 与串口 JSON 协议为主，尚未发现现成 BLE GATT 实现。
- 微信小程序能力：支持 BLE（GATT），不支持 USB 串口。
- OTA：确定采用 **小程序直传原始 `firmware.bin`** 到设备 OTA 分区。

---

## 2. 总体架构

### 2.1 组件

- **WXCody（小程序）**
  - BLE：扫描/连接/订阅 Notify/写入 Write
  - UI：模式/图库/笔记/设置/OTA 页
  - 协议层：JSON 行协议 + 二进制分包协议（用于图片/OTA）
  - 断线重连、进度展示、失败重试

- **Cody 固件（ESP32）**
  - BLE GATT：提供 1 个 Service + RX/TX 两个 Characteristic（“BLE UART”）
  - 协议层：
    - JSON 命令：复用现有 `serial_protocol` 命令语义（cmd 字段等）
    - 二进制帧：图片与 OTA 的高吞吐分包 + ACK/重传
  - OTA 写入：写 OTA 分区，校验后触发重启切换

### 2.2 数据通道（同一条 BLE 连接上双通道复用）

- **通道 A：JSON 行协议**（低吞吐控制/状态/事件）
- **通道 B：Binary Frame**（大吞吐：图片/OTA）

---

## 3. BLE GATT 设计

### 3.1 Service/Characteristic

> UUID 先占位，落地时统一写到固件与小程序常量中。

- **Service UUID**：`CODY_SERVICE_UUID`
- **RX Characteristic UUID**：`CODY_RX_UUID`
  - Properties：`Write Without Response`（必要时也允许 Write）
- **TX Characteristic UUID**：`CODY_TX_UUID`
  - Properties：`Notify`

### 3.2 连接与 MTU

- 小程序连接后：
  1. 启用 Notify（TX）
  2. 尝试协商更大的 MTU（若平台/机型支持）
- **应用层仍必须分包**：不能假设 MTU 足够大或稳定。

---

## 4. 应用层协议

### 4.1 通道 A：JSON 行协议（推荐沿用现有 cmd）

#### 4.1.1 编码

- UTF-8 文本
- 每条消息以 `\n` 结尾（“JSON line”）

#### 4.1.2 示例

```json
{"cmd":"get_mode"}
{"cmd":"set_mode","mode":1}
{"cmd":"image_info"}
{"cmd":"get_notes"}
{"cmd":"set_wifi","ssid":"xxx","psk":"yyy"}
```

#### 4.1.3 事件与错误

- 设备侧仍使用类似：
  - `{"cmd":"wifi_join_result", "ok": true, ...}`
  - `{"cmd":"error","msg":"..."}`
- 小程序需要统一 toast + 日志页记录（可导出）。

### 4.2 通道 B：Binary Frame（图片/OTA）

#### 4.2.1 设计目标

- 稳定：可 ACK、可超时重传
- 简单：第一期采用 **stop-and-wait**（一次只飞 1 包），避免复杂滑窗
- 可扩展：保留 `type/session/seq` 便于后续滑窗与并发

#### 4.2.2 帧格式（小端）

```
MAGIC(2) | TYPE(1) | SESSION(1) | SEQ(2) | LEN(2) | PAYLOAD(LEN) | CRC16(2)
```

- **MAGIC**：固定 `0xC0 0xDY`（落地时定具体值，避免与文本混淆）
- **TYPE**：帧类型（见下）
- **SESSION**：0~255，发起一次上传/下载/OTA 时递增；用于区分旧包
- **SEQ**：0~65535，分块序号
- **LEN**：payload 长度
- **CRC16**：对 header+payload 做 CRC16-CCITT（或其他一致算法）

#### 4.2.3 帧类型（第一期最小集合）

- `ACK` / `NACK`
  - payload：`ackedSeq` / `errCode`
- 图片：
  - `IMG_PULL_BEGIN`：slot、编码（RGB565/JPEG）、总大小（可选）
  - `IMG_PULL_CHUNK`：slot、offset、bytes
  - `IMG_PUSH_BEGIN`：slot、totalBytes、format
  - `IMG_PUSH_CHUNK`：slot、offset、bytes
  - `IMG_PUSH_FINISH`：slot、crc/len
- OTA：
  - `OTA_BEGIN`：totalBytes、sha256(可选)
  - `OTA_CHUNK`：offset、bytes
  - `OTA_FINISH`：sha256/len
  - `OTA_STATUS`：进度/错误码
  - `OTA_APPLY_READY`：设备将重启

> 注：图片是否使用 RGB565 还是 JPEG，需要在实现阶段根据性能与 Flash 写入策略定；建议优先 RGB565（与现有 240×240 RGB565 管线一致），但可选 JPEG 缩略以减 BLE 传输量。

#### 4.2.4 stop-and-wait 时序

- 小程序发送 `CHUNK(seq=n)` → 等待 `ACK(n)`（超时重传，最多 R 次）→ 发送 `CHUNK(n+1)`
- 设备端对重复 `seq`：可幂等 ACK（不重复写入）。

---

## 5. 功能与页面（WXCody）

### 5.1 连接页

- 扫描：按设备名（如 `Cody-` 前缀）或 Service UUID 过滤
- 连接：显示连接状态、RSSI（可选）、MTU（可选）
- 断开/重连

### 5.2 Tabs（与现有 Web 控制台对齐）

- **模式**
  - 展示当前模式
  - 切换模式（图片/时钟/笔记/表情）

- **图库**
  - 槽位列表、当前槽位标记
  - 回显缩略（IMG_PULL）
  - 上传图片（IMG_PUSH）
  - 删除/设为当前

- **笔记**
  - 列表、置顶、增删改
  - 轮播开关与间隔（已在固件侧 constrain 3~60）

- **设置**
  - WiFi 状态、设置 WiFi（JSON `set_wifi`）
  - 存储空间（`fs_space`）
  - 深度格式化（`format_fs`）
  - 恢复出厂（`reset_system`，并提示会断开）

- **OTA**
  - 选择 `firmware.bin`（或从固定 URL 下载到本地再推送）
  - 进度条（发送进度 + 设备回执）
  - 失败重试（重来）

### 5.3 调试页（可选但推荐）

- 最近 N 条 BLE 收发日志
- 一键复制导出

---

## 6. 固件改动点（高层）

- 新增 BLE（建议 NimBLE）：
  - GATT Service + RX/TX characteristic
  - Notify 发送队列（避免阻塞主循环）
- 协议层：
  - JSON line parser：复用现有 `serial_protocol` 处理函数（或抽成共享 handler）
  - 二进制帧 parser：状态机 + CRC + ACK/重传
- OTA：
  - 在 BLE OTA 会话中写 OTA 分区
  - finish 校验通过后设置 boot 分区并重启
- 图片：
  - pull：从 LittleFS 读出并分块发
  - push：边收边写，finish 校验与落盘

---

## 7. 风险与对策

- **BLE 吞吐与稳定性**：图片/OTA 必须走二进制分包 + ACK，不建议纯 JSON/hex/base64。
- **UI 卡顿**：小程序端分块发送要节流，进度刷新也要节流。
- **断线恢复**：第一期可不做断点续传，但要保证失败不切换分区、不损坏已有数据。
- **安全**：第一期可 “Just Works”，但建议后续加一次性配对码或白名单。

---

## 8. 验收标准（第一期）

- 小程序可稳定连接设备（多次连接/断开不死锁）
- 模式切换、WiFi 配置、笔记 CRUD 全通
- 图库：至少支持 1 张图上传与回显
- OTA：可通过 BLE 推送 `firmware.bin` 并成功重启到新版本
- 异常场景：断线/超时能提示并允许重试；不会导致设备反复重启或 OTA 变砖

