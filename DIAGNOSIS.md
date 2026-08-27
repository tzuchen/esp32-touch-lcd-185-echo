# ESP32 Ears 診斷報告

> 日期: 2026-08-27 · 範圍: 只讀代碼（未修改任何 .c/.h）
> 症狀: 子晨對 ESP32 說話，完全無反應（沒聽到、沒回應、LCD 狀態未更新）

## 根因（最可能的 1-2 個原因）

- **根因 1（確鑿）: 韌體裡根本沒有音訊上行路徑。** 整個 repo 不存在 `components/audio_udp/`，全專案 grep 不到任何 UDP/socket/lwIP 代碼。目前韌體只是「本地迴聲」: mic(I2S1 RX) → 2x gain → speaker(I2S0 TX)，PCM 從未離開 ESP32。因此 Jetson 端收不到任何 mic 資料 → VAD 永遠不觸發 → ASR 不跑 → 沒有 TTS → 沒有下行音訊 → 完全無反應。這正是 PLAN.md 定義但從未落地的 Step 2。
- **根因 2（確鑿，獨立可證）: 現狀 `main/app_main.c` 無法通過編譯。** 它呼叫的 `wifi_simple_init_and_connect()` 與 `wifi_simple_get_ip_str()` 兩個符號在 `components/wifi/wifi_simple.h` 中根本沒宣告，而 .c 裡只定義了 `wifi_simple_connect()`。同時 `#if CONFIG_OTA_GITHUB_ENABLE` 引用的 config 在任何 Kconfig 中都不存在（ota/Kconfig 只有 VERSION_URL/FIRMWARE_URL/TIMEOUT_MS，沒有 `OTA_GITHUB_ENABLE`，ota_github.h 也沒宣告該宏）。若這版 main 真的被燒錄，device 會停留在更早的 boot 畫面（甚至 boot 都過不了）—— 與「LCD 卡住/不更新」的觀察一致。

## 證據

- `components/` 目錄只有 `audio_echo/ board/ gfx/ ota/ wifi/` 五個 component，無 `audio_udp/` → 韌體沒有把 mic PCM 送 UDP 的機制（根因 1）。
- 全專案 `grep -rniE 'udp|lwip|socket'` 僅命中 `wifi_simple.c` 內的 `esp_wifi_connect()`（WiFi 連線，非音訊）→ 確認不存在任何音訊 socket 代碼。
- `main/app_main.c:41` → `ESP_ERROR_CHECK(wifi_simple_init_and_connect());`，但 `components/wifi/wifi_simple.h:7` 只宣告 `esp_err_t wifi_simple_connect(void);`，.c:40 定義的也是 `wifi_simple_connect()` → `wifi_simple_init_and_connect()` 未定義，鏈接失敗。
- `main/app_main.c:43` → `show_status("WiFi connected", wifi_simple_get_ip_str());`，但 `wifi_simple.h` 與 .c 均未宣告/定義 `wifi_simple_get_ip_str()` → 未定義符號，鏈接失敗。
- `main/app_main.c:45` → `#if CONFIG_OTA_GITHUB_ENABLE`，而 `components/ota/Kconfig` 未定義 `CONFIG_OTA_GITHUB_ENABLE`、`components/ota/ota_github.h` 未宣告該宏、`ota_github.c:61` 亦引用它 → 未定義宏，預處理器條件無效（預設走 0/1 分支但未明確控制）。
- `main/app_main.c:35` → `gfx_init(board_lcd_get_panel());` 傳一個參數，但 `components/gfx/gfx.h:41` 宣告為 `void gfx_init(void);`（gfx.c:141 實現也是 `gfx_init(void)`）→ 簽名不符，編譯告警/錯誤。
- `main/app_main.c:19` → `gfx_flush();`，但 `gfx.h:43` 只宣告 `void gfx_flush_full(void);`，無 `gfx_flush()` → 未定義符號。
- `main/app_main.c:35` → `board_lcd_get_panel()`，但 `components/board/board_lcd.h` 只宣告 `board_lcd_init/flush/width/height`，無 `board_lcd_get_panel()` → 未定義符號。

### 其他發現（次要問題）

- **音訊 task 堆疊偏小**: `components/audio_echo/audio_echo.c:172` → `xTaskCreatePinnedToCore(echo_task, "echo_task", 4096, ...)` 只有 4KB 堆疊。一旦加 UDP 發送（socket send + lwIP 堆疊會吃幾百 bytes 堆疊 + 512B 音訊 buffer），4KB 極度吃緊，建議 audio task 升到 8192 以上。
- **本地迴聲未與 UDP 下行分離**: `echo_task`（`audio_echo.c:99-106`）持續把 mic 放大 2x 直接寫入 speaker。當 Jetson 回傳 TTS 也寫同一支 I2S0 TX 時，會同時播出「本地迴聲 + Jetson 聲音」，且 mic 會拾取 speaker 聲音 → UDP → 再播 → 回授迴圈。PLAN.md 的「風險」已點出此問題，需加 mute 邏輯（收到 Jetson 音訊時暫停本地 echo）。
- **VAD 與 ASR 都在 Jetson 端，但 Jetson 端 daemon 是否已跑未知**: PLAN.md Step 1 說「寫 `esp32_audio.py` 監聽 UDP 5000」，repo 內沒有此腳本（它在 Jetson 側，非本 repo）。需確認 Jetson 端 `esp32_audio.py` 已部署且在監聽 5000/5001。
- **I2S 時鐘/pin 正確性（暫無反證）**: mic 用 I2S1（WS=2/SCK=15/SD=39, `audio_echo.c:68-70`），speaker 用 I2S0（DOUT=47/BCLK=48/LRCK=38, `audio_echo.c:72-74`），皆 16kHz/16bit/mono，符合 PLAN.md。slot 設 MSB、mic 右声道（`audio_echo.c:164`）。暫無明顯錯誤，但「mic 常為右声道」是常見陷阱，若 INMP441 實際左声道則讀到全 0 → 無音訊。
- **WiFi 同網段**: ESP32 連 `CONFIG_WIFI_SSID`（Kconfig 預設空字串 `wifi/Kconfig:5`），Jetson 在 `192.168.31.0/24`（PLAN.md:16-18）。若 ssid 設定正確則同網段，但 `sdkconfig` 未提交，無法從 repo 確認實際 SSID/IP。

## 建議修復步驟（按順序）

1. **先修 `main/app_main.c` 的編譯錯誤**（根因 2）。二選一：
   - 把 `wifi_simple_init_and_connect()` 改回 `wifi_simple_connect()`、`wifi_simple_get_ip_str()` 改回已取得 IP 的變數、`gfx_init(board_lcd_get_panel())` 改回 `gfx_init()`、`gfx_flush()` 改回 `gfx_flush_full()`、`#if CONFIG_OTA_GITHUB_ENABLE` 換成實際存在的 config。
   - 或若 `wifi_simple` 已重構過，更新 `main/app_main.c` 以匹配新 API。
   目的: 確保 device 能 boot 到「Audio... echo running」，排除更早阶段的阻塞。

2. **新增 `components/audio_udp/`**（根因 1，PLAN.md Step 2）:
   - `audio_udp.c/.h`: 開 UDP socket，bind 5001 收 Jetson 下行 PCM → 寫 I2S0 TX；mic PCM 在 `echo_task` 中多複製一份 sendto Jetson IP:5000。
   - 加 `audio_udp_start(jetson_ip, port_up=5000, port_down=5001)`，在 `app_main.c` 的 `audio_echo_start()` 後呼叫。
   - `components/audio_udp/CMakeLists.txt`: `idf_component_register(SRCS "audio_udp.c" REQUIRES driver lwip)`。
   - 在根 `main/CMakeLists.txt` 的 `REQUIRES` 加上 `audio_udp`。

3. **在 `echo_task` 加 UDP relay + mute 邏輯**: 收到 Jetson 下行音訊時，暫停本地 mic→speaker 迴聲（避免回授），並讓 speaker 只播 Jetson 音訊。

4. **把 audio task 堆疊從 4096 升到 ≥8192**（`audio_echo.c:172`），預留 socket/lwIP 堆疊空間。

5. **確認 Jetson 端 `esp32_audio.py` daemon 已部署且監聽 UDP 5000/5001**（PLAN.md Step 1），用 `nc -ul 5000` 或 Python 收包驗證 ESP32 mic PCM 真的到得 Jetson。

6. **重編譯 + OTA 燒錄**（需子晨在 ESP-IDF 環境），burn 後用 `idf.py monitor` 看 log 確認: `audio echo started` → `audio udp started` → Jetson 端收包成功。

7. **（選用）LCD 顯示 "Listening..."**：當 UDP 連線正常時顯示，讓子晨有視覺回饋，排除「LCD 卡住」的誤判。
