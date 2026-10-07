# ESP32-S3-CAM + ICS-43434（自定义板）

**ESP32-S3-CAM (GOOUUU 板, N16R8)** + **ICS-43434** I²S 数字麦克风的适配板。

## 这不是上游的 `bread-compact-wifi-s3cam`

上游那块板子虽然名字里也有 s3cam，但**接线完全不同**，两者不能互换：

| | 上游 `bread-compact-wifi-s3cam` | 本板 |
|---|---|---|
| 麦克风 | 1 / 2 / 42 | **14 / 21 / 47** |
| 扬声器 | 39 / 40 / 41 | **44 / 1 / 43** = MAX98357A 的 BCLK/LRCK/DIN |
| LCD | ST7789（占 19/20/38/45/47/21） | **ST7789 240×284 横屏**（39/40/41/42 + CS→GND、BL→3V3） |
| 摄像头 | **OV5640**（实测） | 引脚已由原理图实证；**已启用**（长按 BOOT 预览，见下） |

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

## 引脚（⚠️ 板子的引脚**已经用满**）

| 用途 | GPIO | 说明 |
|---|---|---|
| MIC BCLK / WS / DIN | **14 / 21 / 47** | voice_notes 阶段 1 已验证，原样沿用 |
| 屏幕 CLK (SCL) | **39** | 原 SD_CLK |
| 屏幕 MOSI (SDA) | **40** | 原 SD_DATA |
| 屏幕 RST | **41** | 原扬声器预留 |
| 屏幕 DC | **42** | 原扬声器预留 |
| 屏幕 CS | **接 GND** | 腾出 G43 给功放（屏幕是 SPI3 上唯一设备，可常选中） |
| 屏幕 BL (背光) | **接 3V3** | 腾出 G44 给功放；代价：**背光常亮、无调光** |
| BOOT 按键 | 0 | ✅ 实测确认（按下触发 `idle -> connecting`，无唤醒词日志） |
| 摄像头 | 4–13, 15–18 | 引脚已由原理图逐脚实证，**尚未启用** |
| 扬声器 | **44 / 1 / 43** | MAX98357A（BCLK/LRCK/DIN）；G43 上有板载 LED，见下 |

### 引脚预算：怎么把功放塞进去的（2026-10-07 定案）

排针引出 **GPIO1–21、GPIO39–48**。减掉摄像头（4–13、15–18）、麦克风（14/21/47）、
USB（19/20）、内部 Flash/PSRAM（26–37）、strapping（3/45/46）、板载 LED（**2/43/48**）
之后，**能用的只有 1 和 39~44**。

MAX98357A 要 3 根（BCLK/LRCK/DIN），于是这么凑出来：

| 引脚 | 来源 | 代价 |
|---|---|---|
| **G44** → BCLK | 屏幕 BL 改接 **3V3** | **失去背光调光**（常亮最亮） |
| **G43** → DIN | 屏幕 CS 改接 **GND** | 无 —— 屏幕是 SPI3 上唯一设备 |
| **G1** → LRCK | 本来就空闲 | 无 |

⚠️ **G43 上有板载 LED（引脚图标 `LED TX`）** —— 所以把最快的 BCLK 放干净脚 G44，
最不敏感的 DIN 才给 G43。**放音时那颗 LED 会微微发亮，是正常现象。**

⚠️ **strapping 脚不能动**：45 是 VDD_SPI 选压（**乱用有变砖风险**），
46 是**只输入**（S3 特性），都当不了输出。

⚠️ **必须独立 I²S 口**：采集 16k / 播放 24k，采样率不同，
**共用一个 I²S 口做不到** —— 所以功放的三根**不能**蹭麦克风的 BCLK/WS。

**MAX98357A 模块接线**：VIN→**5V**（别接 3V3：功放峰值会拉垮 3V3 连累 ESP32）、
GND→GND、SD→3V3（常使能）、GAIN→悬空(9dB，嫌小声接 GND=15dB)、喇叭 4Ω/8Ω **≥2W**。

### ⚠️ 屏幕：横屏 + 偏移

面板是 **240×284**（小智 v2.3.0 没有这个预设，最接近的是 240×280）。
**横屏**后逻辑分辨率是 **284×240**（`DISPLAY_SWAP_XY=true`），
而 ST7789 的 RAM 是 240×320，所以沿长边要留偏移 = `(320−284)/2` = **18**。

⚠️ **横屏后偏移在 X 轴上** —— 所以是 `DISPLAY_OFFSET_X = 18`（不是 Y）。
`18` 是**算出来的、不是量出来的**，因屏厂而异。

⚠️ **画面错位 / 边缘有彩条 / 出现重复行时，第一个该调的就是它** ——
改 `config.h` 的 `DISPLAY_OFFSET_X` → 重编译 → 看效果。**别去怀疑接线。**

⚠️ **朝向不对**：左右镜像 → 翻 `DISPLAY_MIRROR_X`；上下颠倒 → 翻 `DISPLAY_MIRROR_Y`。
这两项纯试错，一次编译就能定。开机日志会打印当前值，方便对照：
`ST7789 284x240 ready (SPI3, swap_xy=1, offset 18,0, mirror 1,0)`

⚠️ 另外**别在板级代码里再调 `esp_lcd_panel_set_gap`** —— 偏移由 `SpiLcdDisplay`
的构造参数处理，两处都设会叠加两次，画面直接偏出屏幕。（我第一版就写错了。）

## 串口补丁

`main/application.cc` 的 `ShowActivationCode()` 被加了一行 `ESP_LOGW` 打印验证码。

⚠️ **屏幕上【看不到】验证码 —— 这不是 bug，是上游的设计。** 细节看
`ShowActivationCode()`：它确实调了 `Alert()`，但 `Alert()` 显示的是 **`message`**
（"请登录 xiaozhi.me 输入验证码"这类提示语），**不是 `code` 本身**；
6 位数字只被**逐个 `PlaySound()` 播报**，**根本不打字上屏**。

所以拿到验证码只有两条路：

1. **这行串口日志**（推荐 —— 听 6 个数字容易听错、也难复现）
2. 喇叭播报 —— 2026-10-07 接上 MAX98357A 后**已具备**（⚠️ 尚未用耳朵验证过）

→ **这行别删**：串口是最可靠的路径，排障时也最省事。

## 编译

```sh
source /path/to/esp-idf/export.sh    # 本项目用 ESP-IDF v5.5.5（v2.3.0 要求 5.4+）
cd xiaozhi-esp32
idf.py set-target esp32s3
# 选板：把 sdkconfig 里的 CONFIG_BOARD_TYPE_* 切到 ESP32_S3_CAM_ICS43434
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## 摄像头实时预览

**长按 BOOT 约 1 秒**在 xiaozhi UI 与摄像头预览之间切换；**预览中短按**也能退出。
画面 **240×240 居中**，两侧各 22px 黑边（屏宽 284）。**短按 BOOT 仍是"触发对话"，语义未变。**

设计文档：`hello_world/docs/plans/2026-10-07-xiaozhi-camera-preview.md`
实现计划：`hello_world/docs/plans/2026-10-07-xiaozhi-camera-preview-impl.md`

### 三条不能动的约束

1. **上屏走 LVGL 顶层全屏 `lv_image`，不要改成"绕过 LVGL 直推面板"。**
   判断 DMA 完成的 `on_color_trans_done` 回调**已被 esp_lvgl_port 占用**
   （`esp_lvgl_port_disp.c:124`，用于给 LVGL 发 flush-ready）；抢过来会永久搞坏 LVGL，
   而 `esp_lcd` 没有 getter、**无法还原**。退而靠 SPI 队列阻塞则会有约 500ms 预览延迟。
2. **帧数据必须逐像素交换字节序**（`camera_frame_swap_bytes`）。
   依据：上游 `esp32_camera.cc` 对每一帧都做 `__builtin_bswap16` 且默认开启。
3. **退出时 `esp_camera_deinit()` 不能删** —— 它把 230KB 帧缓冲还给 PSRAM。
   不还的话会和 LVGL 的 2MB 图像缓存抢内存。

### 两个实现期踩到的坑（改代码时容易再踩）

- **新增源码后必须 `idf.py reconfigure`**：板级源码靠 `file(GLOB)` 收集
  （`main/CMakeLists.txt:881`，**非递归**），glob 在 configure 时求值并固化进 `build.ninja`。
  **新文件不会被自动收录，而 `idf.py build` 照样报"成功"** —— 极具迷惑性。
  判据：`build/esp-idf/main/CMakeFiles/__idf_main.dir/boards/esp32-s3-cam-ics43434/` 下有对应 `.obj`。
- **`camera_frame.h` 的 `extern "C"` 不能删**：它是 C 头文件、被 C++ 的 `camera_preview.cc` 调用，
  没有它会在**固件链接时**报 `undefined reference`（主机单测是纯 C，**测不出来**）。

### 主机侧单测

```bash
cd main/boards/esp32-s3-cam-ics43434
gcc -Wall -Wextra -o /tmp/t_camframe test/host/test_camera_frame.c camera_frame.c -I. && /tmp/t_camframe
```

⚠️ 测试放 `test/host/` 子目录是**有意的**：板级目录的 `*.c` 会被 glob 编进固件。

## 已知限制

- **音频输出**：2026-10-07 接上 MAX98357A（引脚见上）。固件侧已验证
  （日志有 `codec ready ... spk 24000 Hz` 且 `Set output enable to true`，
  **不再有**"跳过 TX 通道"告警），但**"能不能真的出声"还没用耳朵听过** ——
  需唤醒一次小智听回复来确认。
- **背光不可调**：BL 接 3V3 常亮（为功放腾出 G44）。因此 `self.screen.set_brightness`
  这个 MCP 工具**不再注册** —— `mcp_server.cc:66` 对空 backlight 有 `if` 保护，不会崩。
- 摄像头引脚已记录但**未初始化**。
