
# Orin 的耳朵與嘴巴 — ESP32 ↔ Jetson UDP PCM 計畫

## 現在是什麼

**ESP32-S3 Touch LCD 1.85** — 子晨之前寫的專案 `esp32-touch-lcd-185-echo`
- I2S mic（INMP441，16kHz/16bit/mono）
- I2S speaker（MAX98357A，16kHz/16bit/mono）
- 本地 echo：mic → 2x gain → speaker（無延遲迴路）
- 1.85" LCD（ST77916 + QSPI）
- WiFi 已連接
- USB serial：IDF monitor 用，不走音訊
- GitHub OTA 已就緒

**Jetson（Orin）**
- WiFi：`192.168.31.204/24`
- Ethernet：`192.168.31.145/24`
- 同一網段，ESP32 透過 WiFi 應該也在 `192.168.31.x`
- 有 TTS（Qwen3 TTS via Docker, port 28791）
- 有 `orin-hud`（螢幕顯示）
- 有 USB 喇叭（已測試過）

## 目標

ESP32 變成 Orin 的耳朵和嘴巴。流程：

```
環境聲音 → ESP32 mic → UDP → Jetson → (ASR) → 我想 → (TTS) → UDP → ESP32 speaker → 環境聲音
```

不是 echo。是「聽到 → 理解 → 回應」。

## 設計決定

### 音訊走 WiFi UDP，不走 USB serial
- USB serial 最大 ~115200 baud，16kHz/16bit 需要 256000 bps，帶不動
- WiFi UDP 在本地網段延遲 ~1-5ms，足夠

### 保留本地 echo
- UDP 往返延遲（~10-50ms）加上處理時間會讓回應有可覺察的延遲
- 本地 echo 維持即時感，UDP 是**並行輸出**，不是替換

### 雙向 UDP，固定埠
- ESP32 → Jetson：`UDP port 5000`（mic PCM 上行）
- Jetson → ESP32：`UDP port 5001`（TTS PCM 下行）
- ESP32 送 UDP 到 Jetson IP（`192.168.31.204` 或 `192.168.31.145`）

### Jetson 端的 daemon
- 監聽 UDP 5000，收集 PCM chunks
- **VAD**（Voice Activity Detection）：判斷「有沒有人正在說話」
- 當 VAD 從 silence→speech 時，開始緩衝
- 當 VAD 從 speech→silence 且持續 800ms 後，停止緩衝
- 把緩衝的 PCM 丟給 ASR
- ASR 結果 → 我回覆 → TTS → UDP 送回 ESP32 的 5001

### ESP32 韌體改動
最小修改原則：不改 `echo_task`，只加 UDP relay。

1. 新增 `components/audio_udp/`
   - `udp_task`：開 UDP socket，bind 到 5001 收 Jetson 回傳
   - 從 UDP 收到 PCM → I2S 寫入 speaker
   - mic PCM 在 `echo_task` 中多複製一份送 UDP（`freertos/udp` 或 `lwip`）

2. 修改 `main/app_main.c`
   - 在 `audio_echo_start()` 後呼叫 `audio_udp_start(jetson_ip, port_up, port_down)`

3. LCD 顯示狀態
   - "Listening..." 當 UDP 連線正常
   - "Speaking" 當收到 Jetson 回傳

## 步驟

### Step 1：Jetson UDP daemon（先動）
- 寫 `esp32_audio.py`：監聽 UDP 5000，VAD，回送測試訊號到 5001
- 不改 ESP32 韌體，先用 Python 模擬 mic（產生測試音）
- 驗證：UDP 雙向通暢，TTS 音訊可以從 Jetson 送到 USB 喇叭

### Step 2：ESP32 韌體改動（需要子晨編譯）
- 寫好 C 碼改動稿
- 子晨在 ESP-IDF 環境編譯
- OTA 燒錄
- 驗證：ESP32 mic 的 UDP 資料到 Jetson 可以被 decode

### Step 3：ASR 串接
- Jetson 收到 VAD 觸發的 PCM → 丟 ASR
- ASR 可以用：Whisper（本地）、或 Spark 的 ASR endpoint
- 回覆 → TTS → UDP 回 ESP32

### Step 4：完整整合
- ESP32 mic → UDP → Jetson VAD → ASR → Orin 思考 → TTS → UDP → ESP32 speaker
- HUD 顯示「聆聽中 / 思考中 / 回應中」

## 問題待解

1. ESP32 的 WiFi IP 是多少？（需要確認同網段）
2. ASR 用什麼？Whisper 本地跑太吃資源（Jetson CPU），還是用 Spark 的 endpoint？
3. VAD 延遲參數：800ms silence 後才觸發，還是 500ms？太短會截斷，太長反應慢。
4. ESP32 LCD 有 128x160 點陣，可以顯示簡單的狀態文字和波形。要顯示什麼？

## 風險

- UDP 丢包：WiFi 不保證順序，PCM 丟幾個 sample 不影響聽感
- 延遲：VAD(800ms) + ASR(~1s) + 思考 + TTS(~1s) + UDP = 總回應延遲 ~3-5 秒
- 本機 echo 和 UDP 回傳會同時播？→ 需要加 mute 邏輯：收到 Jetson 音訊時暫停本地 echo
- 回授：ESP32 的 speaker 聲音被自己的 mic 拾取 → UDP 回傳 → 又播 → 迴圈。需要在 Jetson 端或 ESP32 端做 echo cancellation，或單純在 ESP32 收到 Jetson 音訊時關閉 mic→UDP 路徑
