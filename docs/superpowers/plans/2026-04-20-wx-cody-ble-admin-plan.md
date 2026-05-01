# WXCody BLE Admin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建微信小程序 `WXCody`，通过 BLE 直连 Cody（ESP32）实现模式/图库/笔记/设置/OTA（直传 `firmware.bin`）。

**Architecture:** 设备侧新增 BLE GATT（1 Service + RX Write + TX Notify），承载两类消息：JSON 行协议（控制/状态）与二进制分包帧（图片/OTA）。小程序侧做 BLE 传输层、协议封装、UI 与进度/重试。

**Tech Stack:** ESP32 Arduino（建议 NimBLE-Arduino 或 ESP-IDF NimBLE 适配到 Arduino）、LittleFS、OTA（Update/esp_ota）；微信小程序（WXML/WXSS/JS/TS），`wx.*Bluetooth*` BLE API。

---

## File structure（落地前先锁定）

### Firmware（`d:\mochi\Cody\sketch_apr15a`）

**Create:**
- `sketch_apr15a/include/ble/cody_ble.h`：BLE init/start/stop、连接状态、发送队列接口
- `sketch_apr15a/src/ble/cody_ble.cpp`：GATT 服务与特征值、notify/write 回调
- `sketch_apr15a/include/ble/ble_proto.h`：二进制帧结构、CRC、帧类型枚举
- `sketch_apr15a/src/ble/ble_proto.cpp`：帧解析状态机、ACK/NACK、会话状态
- `sketch_apr15a/include/ble/ble_ota.h`：BLE OTA 会话（begin/chunk/finish）
- `sketch_apr15a/src/ble/ble_ota.cpp`
- `sketch_apr15a/include/ble/ble_image.h`：BLE 图库 pull/push
- `sketch_apr15a/src/ble/ble_image.cpp`

**Modify:**
- `sketch_apr15a/include/globals.h`：BLE 连接状态与版本/能力标记（如需要）
- `sketch_apr15a/sketch_apr15a.ino`：setup/loop 中初始化 BLE；桥接 BLE 收到的 JSON 到现有 `processSerialCommand`
- `sketch_apr15a/src/protocol/serial_protocol.cpp`：抽出可复用 handler（若需要）/新增 capability 查询命令（可选）

### Mini Program（新项目：`d:\mochi\WXCody`）

**Create:**
- `WXCody/project.config.json`
- `WXCody/app.json` / `WXCody/app.ts`
- `WXCody/app.wxss`
- `WXCody/pages/connect/*`：扫描/连接/订阅 notify
- `WXCody/pages/home/*`：tab 容器
- `WXCody/pages/mode/*`
- `WXCody/pages/gallery/*`
- `WXCody/pages/notes/*`
- `WXCody/pages/settings/*`
- `WXCody/pages/ota/*`
- `WXCody/pages/logs/*`（推荐）
- `WXCody/services/ble.ts`：BLE 连接、MTU/notify、write 队列
- `WXCody/services/proto_jsonl.ts`：JSON line 编码/解码、请求/响应与事件分发
- `WXCody/services/proto_bin.ts`：二进制帧打包/解析、stop-and-wait ACK 重传
- `WXCody/services/cody_api.ts`：对上层提供 `getMode/setMode/.../otaBegin/otaChunk/...`
- `WXCody/utils/crc16.ts`
- `WXCody/utils/throttle.ts`

---

## Task 1: 固件侧 BLE 依赖与最小 GATT（只连通）

**Files:**
- Create: `sketch_apr15a/include/ble/cody_ble.h`
- Create: `sketch_apr15a/src/ble/cody_ble.cpp`
- Modify: `sketch_apr15a/sketch_apr15a.ino`

- [ ] **Step 1: 选定 BLE 库并验证能编译**
  - 推荐优先：NimBLE（占用更低、稳定性更好）
  - 验证：固件能编译并运行，手机能扫描到设备名（如 `Cody-XXXX`）。

- [ ] **Step 2: 实现 GATT：1 Service + RX Write + TX Notify**
  - RX：`Write Without Response`
  - TX：`Notify`
  - 连接/断开事件：更新一个全局 `bool g_bleConnected`

- [ ] **Step 3: 在 `setup()` 初始化 BLE**
  - 与 WiFi/屏幕初始化并存，不阻塞主循环。

- [ ] **Step 4: 验证**
  - 用任意 BLE 调试工具（手机）订阅 TX，向 RX 写入测试字节，设备能收到并回一条 Notify（哪怕只是 "OK\n"）。

- [ ] **Step 5: Commit**
  - `git add ...` + `git commit -m "feat: add BLE GATT skeleton for WXCody"`

---

## Task 2: BLE 通道 A —— JSON 行协议桥接到现有 `serial_protocol`

**Files:**
- Modify: `sketch_apr15a/src/protocol/serial_protocol.cpp`
- Modify: `sketch_apr15a/sketch_apr15a.ino`
- Modify: `sketch_apr15a/src/ble/cody_ble.cpp`

- [ ] **Step 1: 抽出“处理一条 JSON payload”入口（可复用）**
  - 目标：串口与 BLE 共用同一套命令处理。
  - 方式：保持 `processSerialCommand(const String& payload)` 不变，BLE 侧直接调用它即可。

- [ ] **Step 2: BLE RX 实现行缓冲（以 `\n` 分割）**
  - 与 CodyDesk 的串口 reader 类似：按字节累积，遇到 LF 切行。
  - 每行 trim 后若以 `{` 开头则走 `processSerialCommand`。

- [ ] **Step 3: 将 `Serial` 输出重定向/镜像到 BLE TX（仅对协议响应）**
  - 关键：现有 `emitWifiJoinResultEvent` 等直接写 `Serial`。
  - 方案：
    - 最小改动：在 `processSerialCommand` 内对返回 JSON 增加一个 `serializeJson(doc, bleTx)` 的路径；或
    - 在 `Serial` 输出处改为 `emitJsonLine(doc)`，同时发 Serial + BLE（推荐统一出口）。

- [ ] **Step 4: 验证命令闭环**
  - 手机写入：`{"cmd":"get_mode"}\n`
  - 设备 Notify 返回：`{"cmd":"get_mode","mode":...}`（或你当前协议格式）

- [ ] **Step 5: Commit**
  - `git commit -m "feat: bridge BLE JSONL to existing serial protocol"`

---

## Task 3: BLE 通道 B —— 二进制帧协议（ACK stop-and-wait）

**Files:**
- Create: `sketch_apr15a/include/ble/ble_proto.h`
- Create: `sketch_apr15a/src/ble/ble_proto.cpp`
- Modify: `sketch_apr15a/src/ble/cody_ble.cpp`
- Create: `WXCody/utils/crc16.ts`（与固件 CRC16 对齐）
- Create: `WXCody/services/proto_bin.ts`

- [ ] **Step 1: 固件端定义帧结构与 CRC16**
  - Frame: `MAGIC(2) TYPE(1) SESSION(1) SEQ(2) LEN(2) PAYLOAD LEN CRC16(2)`
  - 选择 CRC16：CCITT-FALSE（多平台好实现）并写明 poly/init/xorout。

- [ ] **Step 2: 固件端解析状态机**
  - 能从 BLE RX 字节流中切帧
  - CRC 错：NACK（含错误码）
  - 正常：ACK 当前 seq

- [ ] **Step 3: 小程序端实现同样的打包/解包 + stop-and-wait**
  - `sendFrame(type, session, seq, payload)` 返回 Promise，等 ACK(seq) resolve
  - 超时：重发（默认 3 次）
  - 失败：抛错，UI 提示“可重试”

- [ ] **Step 4: 验证**
  - 用小程序发一个自定义 ping frame（payload="ping"），固件回 ACK，并可回一条 notify frame（可选）。

- [ ] **Step 5: Commit**
  - 固件与小程序各自一次提交，或拆成两次提交（推荐拆开）。

---

## Task 4: BLE 图片协议（pull + push）

**Files:**
- Create: `sketch_apr15a/include/ble/ble_image.h`
- Create: `sketch_apr15a/src/ble/ble_image.cpp`
- Modify: `sketch_apr15a/src/ble/ble_proto.cpp`
- Create: `WXCody/pages/gallery/*`
- Modify: `WXCody/services/cody_api.ts`

- [ ] **Step 1: 固件端：定义图片帧类型与 payload**
  - `IMG_PULL_BEGIN(slot)`
  - `IMG_PULL_CHUNK(slot, offset, bytes...)`
  - `IMG_PUSH_BEGIN(slot, totalLen)`
  - `IMG_PUSH_CHUNK(slot, offset, bytes...)`
  - `IMG_PUSH_FINISH(slot, totalLen, crc32/sha256?)`（第一期可仅 len）

- [ ] **Step 2: 固件端：pull**
  - 从 LittleFS `/img{slot}.bin` 读取
  - 分块发 `IMG_PULL_CHUNK`（每块大小按 BLE 实测：建议 256~512）
  - 小程序端组装后生成预览（可先只显示“已拉取/进度”）

- [ ] **Step 3: 固件端：push**
  - begin 创建临时文件（类似现有 upload 流程）
  - chunk 按 offset 写入
  - finish 校验长度后 rename 成目标文件，并刷新 `image_info`

- [ ] **Step 4: 小程序端 UI**
  - 槽位卡片（3 个）
  - 拉取进度（节流刷新，避免卡）
  - 上传：从相册选择→压到 240×240→转 RGB565→分块发送

- [ ] **Step 5: 验证**
  - 上传 1 张图到 slot1 → 设备显示/保存正确
  - pull 回显 slot1 → 小程序预览一致

- [ ] **Step 6: Commit**
  - `git commit -m "feat: BLE image pull/push protocol and gallery UI"`

---

## Task 5: BLE OTA（直传 firmware.bin）

**Files:**
- Create: `sketch_apr15a/include/ble/ble_ota.h`
- Create: `sketch_apr15a/src/ble/ble_ota.cpp`
- Modify: `sketch_apr15a/src/ble/ble_proto.cpp`
- Create: `WXCody/pages/ota/*`
- Modify: `WXCody/services/cody_api.ts`

- [ ] **Step 1: 固件端 OTA 会话**
  - `OTA_BEGIN(totalLen)`
  - `OTA_CHUNK(offset, bytes...)`
  - `OTA_FINISH(totalLen, sha256?)`（第一期可只校验长度 + esp_ota_end 返回值）
  - 成功：发送 JSON 事件或二进制 `OTA_APPLY_READY`，并 `ESP.restart()`

- [ ] **Step 2: 小程序端 OTA UI**
  - 选择文件（`firmware.bin`）
  - 进度（发送进度 + 设备 ACK）
  - 错误展示（超时/重传失败/设备拒绝）

- [ ] **Step 3: 验证**
  - 推送已知固件到测试板：完成后重启且版本号变化（通过现有 `ota_info` 或 `version.h` 相关指令显示）

- [ ] **Step 4: Commit**
  - `git commit -m "feat: BLE OTA firmware.bin upload"`

---

## Task 6: WXCody 全功能整合与体验打磨

**Files:**
- Create/Modify: `WXCody/pages/*`
- Modify: `WXCody/services/ble.ts`、`proto_jsonl.ts`、`proto_bin.ts`

- [ ] **Step 1: 断线处理**
  - UI 自动回到未连接
  - 传输中断给出可重试按钮

- [ ] **Step 2: 节流与队列**
  - BLE write queue（避免并发写导致失败）
  - UI 更新节流（进度每 50~100ms 更新一次）

- [ ] **Step 3: 日志页**
  - 展示最近 N 条收发（文本与帧摘要）
  - 一键复制

- [ ] **Step 4: Commit**
  - `git commit -m "feat: WXCody UX polish (reconnect, throttling, logs)"`

---

## Task 7: 文档与发布

**Files:**
- Create: `WXCody/README.md`
- Modify: `Cody/README.md`（如需要加小程序说明）

- [ ] **Step 1: WXCody README**
  - 如何用微信开发者工具打开
  - iOS/Android 权限注意事项
  - 如何连接设备、如何 OTA、常见错误

- [ ] **Step 2: Commit**
  - `git commit -m "docs: add WXCody usage and troubleshooting"`

---

## Self-review checklist

- 覆盖 spec：BLE-only、包含图库与 OTA、复用 JSON cmd、二进制分包 ACK
- 无占位词：已给出文件路径与步骤；UUID/CRC 参数在 Task 3 需要定稿并两端一致
- 一致性：stop-and-wait 先落地，后续可升级滑窗（不在第一期范围）

