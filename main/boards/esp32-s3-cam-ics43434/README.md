# ESP32-S3-CAM + ICS-43434（自定义板）

AI-Thinker **ESP32-S3-CAM (N16R8)** + **ICS-43434** I²S 数字麦克风的适配板。

## 这不是上游的 `bread-compact-wifi-s3cam`

上游那块板子虽然名字里也有 s3cam，但**接线完全不同**，两者不能互换：

| | 上游 `bread-compact-wifi-s3cam` | 本板 |
|---|---|---|
| 麦克风 | 1 / 2 / 42 | **14 / 21 / 47** |
| 扬声器 | 39 / 40 / 41 | **41 / 42 / 43**（预留，未验证） |
| LCD | ST7789（占 19/20/38/45/47/21） | **无** —— 与上面麦克风引脚冲突 |
| 摄像头 | OV2640，AI-Thinker 映射 | 同左（引脚已记录，**本阶段未启用**） |

按 `docs/custom-board_zh.md` 的要求，IO 不同的板子必须独立成板 ——
否则 OTA 会拿官方同型号固件把本固件顶掉。

## 为什么自带 `ics43434_codec`

上游 `NoAudioCodecSimplex` 把麦克风 I²S 配成 `I2S_SLOT_MODE_MONO` + 32bit 槽
= **每 WS 帧 32 个 SCK**。而 **ICS-43434 要求每帧正好 64 个 SCK** ——
用上游配置麦克风**完全不出数据**（不是音质问题，是没数据）。

本板 codec 的两处差异：

1. 麦克风改成 `I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(32BIT, STEREO)` = 64 SCK/帧
2. `Read()` 用 `>>16`（上游是 `>>12`，那是按 INMP441 调的音量增益）。
   ICS-43434 的 24bit 数据在 32bit 字的 `[31:8]`，取左声道后 `>>16` 才对。

这套参数与 `voice_notes` 阶段 1 已验证的 `i2s_mic.c` **逐项一致**，没有重新推导。

## 引脚

| 用途 | GPIO | 说明 |
|---|---|---|
| MIC BCLK | 14 | voice_notes 已验证 |
| MIC WS | 21 | 同上 |
| MIC DIN | 47 | 同上 |
| SPK BCLK | 41 | ⚠️ 预留值，无硬件未验证 |
| SPK LRCK | 42 | 同上 |
| SPK DOUT | 43 | 同上（UART0 TX，console 走 USB 故空闲） |
| BOOT 按键 | 0 | ✅ 实测确认（按下触发 `idle -> connecting`，无唤醒词日志） |
| 摄像头 | 4–13, 15–18 | 本阶段未启用 |

**已避开的引脚**：19/20(USB)、26–37(Flash+八线 PSRAM)、38–40(SD)、
45/46(strapping)、48(板载 LED)、0/3(strapping)

## 串口补丁

`main/application.cc` 的 `ShowActivationCode()` 被加了一行 `ESP_LOGW` 打印验证码。
**这是本板的必需品，不是调试残留** —— 本板没有屏幕也没有扬声器，而上游只把
验证码交给屏幕和语音播报两条路，不补这行就永远激活不了设备。

将来若配上屏幕或喇叭，可以删掉。

## 编译

```sh
source /path/to/esp-idf/export.sh    # 本项目用 ESP-IDF v5.5.5（v2.3.0 要求 5.4+）
cd xiaozhi-esp32
idf.py set-target esp32s3
# 选板：把 sdkconfig 里的 CONFIG_BOARD_TYPE_* 切到 ESP32_S3_CAM_ICS43434
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## 已知限制

- **无扬声器**：能连服务器、能采音，但听不到回复；AEC 也无法工作
  （`input_reference` 需要喇叭回采）。
- 扬声器引脚是**预留值**，等硬件到位后需要在真机上验证。
- 摄像头引脚已记录但**未初始化**。
