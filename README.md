# ESP32-S3 Touch LCD 1.85 — Mic→Speaker Echo + Wi‑Fi + GitHub OTA + LCD Status

目標：用 **ESP-32-Touch-LCD-1.85 (ESP32‑S3)** 做一個最小可用專案：
- 開機連 Wi‑Fi（Station）
- LCD 顯示狀態字串
- 麥克風即時回放到喇叭（I2S RX→TX）
- 用 GitHub 當 OTA 來源（不需要自己架伺服器；只要 GitHub Raw / Releases）

## 需求
- ESP‑IDF v5.x（建議 5.1+）
- 你的板子：ESP-32-Touch-LCD-1.85（pin 已寫死在 `components/board/board_lcd.c` / `components/audio_echo/audio_echo.c`）

## 編譯 / 燒錄
```bash
idf.py set-target esp32s3
idf.py menuconfig
idf.py build flash monitor
```

### menuconfig 要設的項目
- `Wi-Fi`：填 `Wi-Fi SSID` / `Wi-Fi Password`
- `GitHub OTA`（可選）：
  - `Version file URL`：一個純文字檔，內容只有版本字串（例如 `1.0.3`）
  - `Firmware binary URL`：指向韌體 bin 的 URL（建議用 GitHub Releases）

## OTA 怎麼放檔案（建議做法）
1. 你的 repo 建一個 `ota/version.txt`，內容例如：
   ```
   1.0.3
   ```
2. 每次要 OTA：
   - `idf.py build`
   - 把 `build/esp32_touch_lcd_185_echo.bin` 上傳到 GitHub Release，檔名例如 `esp32_touch_lcd_185_echo.bin`
3. menuconfig 填：
   - Version URL（raw）：
     `https://raw.githubusercontent.com/<user>/<repo>/main/ota/version.txt`
   - Firmware URL（release）：
     `https://github.com/<user>/<repo>/releases/latest/download/esp32_touch_lcd_185_echo.bin`

開機後連上 Wi‑Fi 就會：
- 抓 `version.txt`
- 跟目前 app 版本（`idf.py menuconfig -> Application version`）比
- 不同就走 `esp_https_ota` 更新

## 備註
- LCD：用 ST77916 + QSPI，並透過 TCA9554 I/O expander 去拉 LCD reset。
- 字型：只有很陽春的 5x7 ASCII（夠用來顯示狀態）。
- 音訊：16kHz / 16-bit / mono，mic→speaker，預設有簡單增益（1.5x），可在 `audio_echo.c` 調。

