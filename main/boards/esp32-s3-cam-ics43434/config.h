#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

/* ==================================================================
 * ESP32-S3-CAM (GOOUUU 板, N16R8) + ICS-43434 数字麦
 *
 * 这块板子**不是**上游的 bread-compact-wifi-s3cam —— 那个板子用的是
 * 面包板自接线的引脚（MIC 1/2/42, SPK 39/40/41）并挂了一块 SPI 屏，
 * 与这里的接线完全不同。所以必须独立成板。
 * 见 docs/custom-board_zh.md：IO 配置不同的板子必须新建，否则 OTA 会用
 * 官方同型号固件把本固件顶掉。
 * ================================================================== */

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

/* ICS-43434 走独立的 I²S 口，与扬声器分开：
 * 两者采样率不同（16k 采集 / 24k 播放），共用一个 I²S 口做不到。 */
#define AUDIO_I2S_METHOD_SIMPLEX

/* ------------------------------------------------------------------
 * 麦克风 —— 与 voice_notes 阶段 1 已验证的接线**完全一致**，不要改
 *
 * ⚠️ ICS-43434 要求每 WS 帧正好 64 个 SCK。因此 I²S 必须配成
 *    STEREO + 32bit 槽（2 槽 × 32 = 64 SCK/帧），取左声道后 >>16。
 *    上游 codec 用的是 MONO + 32bit = 32 SCK/帧，**麦克风会直接不工作** ——
 *    这就是本板自带 ics43434_codec 的原因，不要去用 NoAudioCodecSimplex。
 * ------------------------------------------------------------------ */
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_14   /* I²S BCLK */
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_21   /* I²S WS / LRCLK */

/* ⚠️ GPIO47 —— 本板能用，但**取决于模组变体**，换模组前必读
 *
 * ESP32-S3-WROOM-1 数据手册 v1.4 脚注 c：
 *   内置芯片为 **ESP32-S3R16V**（16 MB PSRAM）的模组，VDD_SPI 被设为 1.8 V，
 *   **GPIO47 / GPIO48 的电平也随之是 1.8 V**。
 *
 * 本板是 **N16R8 = ESP32-S3R8**（8 MB PSRAM）→ 脚注 c **不适用**，
 * GPIO47 是正常 3.3 V，**当前这行接线安全**。
 *
 * ⚠️ **但若换成 N16R16V（16 MB PSRAM）的模组，这一行必须挪走** ——
 *    否则麦克风信号电平不匹配。症状会是"能编译、能启动、麦克风就是没数据"，
 *    而且极难联想到是模组变体的问题。
 *
 * → 推论：「哪些引脚干净」的结论是**绑定模组变体**的，换模组要重新核对。 */
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_47   /* 麦克风 SD → ESP32 */

/* ------------------------------------------------------------------
 * 扬声器：MAX98357A（I²S 功放）—— 2026-10-07 接上，本板**有音频输出**了
 *
 * 引脚从哪来（屏幕让出背光脚 + 两根空闲脚）：
 *   BCLK ← G44  原屏幕背光 BL，改接 3V3 常亮后腾出
 *   LRCK ← G1   空闲脚（ADC1_CH0/T1，无 LED、非 strapping）
 *   DIN  ← G2   ⚠️ 2026-10-07 实验后从 G43 挪来 —— 原因见下面「屏幕」那节的 CS 结论
 *
 * ⚠️ **G2 上有板载 LED**（引脚图标 `LED ON`），所以放音时那颗 LED 会闪 ——
 *    **正常现象，不是故障**。之所以不用更"干净"的 G43：那个脚被屏幕的 CS 占着，
 *    而**本模块的 CS 必须由 GPIO 驱动**（下面是实测结论，不是推测）。
 *
 * ⚠️ 必须走**独立 I²S 口**（`AUDIO_I2S_METHOD_SIMPLEX`，见文件头）：
 *    采集 16k / 播放 24k，采样率不同，**共用一个 I²S 口做不到**，
 *    所以这三根**不能**蹭麦克风的 BCLK/WS。
 *
 * 模块接线：VIN→**5V**（⚠️ 别接 3V3：功放峰值电流会拉垮 3V3、连累 ESP32）、
 *          GND→GND、SD→3V3（常使能，没多余脚做开关）、GAIN→悬空(9dB)、
 *          喇叭 4Ω/8Ω **≥2W**。
 * ------------------------------------------------------------------ */
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_44
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_1
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_2    /* ⚠️ 二分排查中：从 G43 挪到 G2 给屏幕 CS 让位 */

/* ==================================================================
 * 显示：ST7789 **240×284** SPI 屏（8 脚模块：GND/VCC/SCL/SDA/RST/DC/CS/BL）
 *
 * 引脚分配（2026-10-07 三次调整后的**定案**）：
 *   屏幕脚        接到          GPIO   这脚的来历
 *   3 SCL  时钟   DISPLAY_CLK    39     SD_CLK（SD 槽因 GPIO38 未引排针，本就不可用）
 *   4 SDA  数据   DISPLAY_MOSI   40     SD_DATA（同上）
 *   5 RST  复位                  41     JTAG MTDI，不外接调试器时可用
 *   6 DC   数据/命令             42     JTAG MTMS，同上
 *   7 CS   片选   DISPLAY_CS     43     ⚠️ **必须由 GPIO 驱动** —— 见下面的实测结论
 *   8 BL   背光   → **接 3V3**    —     不再是 GPIO：背光常亮，**失去调光能力**
 *
 * ⚠️⚠️ **本模块的 CS 不能接地、也不能置 NC —— 实测结论（2026-10-07）**
 *
 *    起因：给功放腾引脚时把 CS 改接 GND（固件 `DISPLAY_CS_PIN = GPIO_NUM_NC`），
 *    结果**屏幕全黑、但背光正常亮**。逐项排除后确认是 CS 的问题：
 *
 *    | CS 接法 | 结果 |
 *    |---|---|
 *    | 物理接 GND + 固件 `GPIO_NUM_NC` | ❌ **全黑** |
 *    | 由 GPIO 驱动（G43） | ✅ 正常 |
 *
 *    ⚠️ **上游 esp-hi / minsi-k08 等板正是用 `GPIO_NUM_NC` 的**（同为 4 线 SPI +
 *    ST7789），**但本模块不行** —— 所以"上游这么写"**不能**当作本板可用的依据。
 *
 *    ⚠️ **排障陷阱（浪费了很久）**：这个故障**用万用表通断档查不出来** ——
 *    "CS 已可靠接地""VCC/GND/SCL/SDA/RST/DC 全通"这些检查**全部会通过**，
 *    屏幕依然是黑的。因为问题不在"有没有接上"，而在"**片选不能常低**"。
 *    唯一的判据：**把 CS 换回 GPIO 驱动，看屏幕是否亮**。
 *
 * ⚠️⚠️ **接线必须按板上的丝印字（G39/G40/…）接，绝不要按"排针从上往下数第几个"接。**
 *
 *    2026-10-07 实测更正：右侧排针**不是** 39,40,…,44 顺排，实际顺序（从上往下）是
 *    ```
 *    G43  G44  G1  G2  G42  G41  G40  G39  G38  G37  G36  G35  G0  G45  G48  G47  G21  G20  G19  GND
 *    ```
 *    （出处：`hello_world/docs/modules/ESP32S3CAM-Pin.jpg`，板子 PCB 上也印了同样的丝印。）
 *
 *    → **背光（BL→G44）在右侧排针里是【第 2 个】；G39 是【第 8 个】。**
 *    早先"顺排"的说法是错的，照它接会把六根线全部错位 ——
 *    实际接到 G43/G44/G1/G2/G42/G41，背光落到 G41（固件不驱动）→ **屏幕全黑、背光不亮**。
 *
 * ⚠️ 面板是 **240×284**（小智 v2.3.0 没有这个预设，最接近的是 240×280）。
 *    横屏后逻辑分辨率是 **284×240**，而 ST7789 的 RAM 是 240×320，
 *    所以沿长边（现在是 X 轴）要留偏移 = (320−284)/2 = **18**。
 *
 * ⚠️ **画面错位 / 边缘有彩条 / 出现重复行时，第一个该调的是
 *    `DISPLAY_OFFSET_X`**（横屏后偏移在 X 轴上）—— 18 是**算出来的、不是量出来的**，
 *    因屏厂而异。改它 → 重编译 → 看效果，**别去怀疑接线**。
 *
 * ⚠️⚠️ **横屏下两个镜像开关的轴是【反的】—— 这是最容易调错的地方：**
 *
 *    | 你看到的 | 该翻哪一项 |
 *    |---|---|
 *    | **上下颠倒** | **`DISPLAY_MIRROR_X`** ← 反直觉 |
 *    | **左右镜像** | **`DISPLAY_MIRROR_Y`** |
 *
 *    原因：`SWAP_XY` 把行列交换了，MADCTL 的 MX/MY 也随之换轴。
 *    **别按竖屏时的直觉去翻** —— 2026-10-07 实测：把 `MIRROR_X` 从 true 改 false
 *    才修好"上下颠倒"。（上游 `bread-compact-wifi-lcd` 用 `MIRROR_X=true`，
 *    那是**另一块面板**的基线，不能照抄。）
 * ================================================================== */
#define DISPLAY_CLK_PIN         GPIO_NUM_39
#define DISPLAY_MOSI_PIN        GPIO_NUM_40
#define DISPLAY_RST_PIN         GPIO_NUM_41
#define DISPLAY_DC_PIN          GPIO_NUM_42
#define DISPLAY_CS_PIN          GPIO_NUM_43   /* ⚠️ 回到原来的 CS 脚（二分排查中，见 README）*/
#define DISPLAY_BACKLIGHT_PIN   GPIO_NUM_NC   /* 物理接 3V3，背光常亮（无调光） */

#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH                   284    /* 横屏：原 240 */
#define DISPLAY_HEIGHT                  240    /* 横屏：原 284 */
#define DISPLAY_OFFSET_X                18     /* 横屏：偏移随 swap 换到 X 轴（= 原 OFFSET_Y）*/
#define DISPLAY_OFFSET_Y                0      /* 横屏：原 18 */
#define DISPLAY_MIRROR_X                false  /* ⚠️ 横屏下这一项管【上下】—— 2026-10-07 实测修正 */
#define DISPLAY_MIRROR_Y                true   /* ⚠️ 横屏下这一项管【左右】—— 2026-10-07 实测定案 */
#define DISPLAY_SWAP_XY                 true   /* ← 横屏核心开关（原 false）*/
#define DISPLAY_INVERT_COLOR            true
#define DISPLAY_RGB_ORDER               LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false  /* 已无意义：背光接 3V3 常亮 */
#define DISPLAY_SPI_MODE                0

/* 板载 BOOT 按键。
 * ✅ 2026-10-04 实测确认：按下触发 idle -> connecting，
 *    且该次状态迁移【没有】伴随 `Wake word detected` 日志 ——
 *    证明走的是按键路径而不是唤醒词路径。 */
#define BOOT_BUTTON_GPIO        GPIO_NUM_0

/* ==================================================================
 * 摄像头 —— 板上实际装的是 **OV5640**（2026-10-07 实测：
 * esp32-camera 打印 `Detected OV5640 camera`，PID=0x5640），
 * 不是早期文档里写的 OV2640。
 *
 * ⚠️ 这不影响接线：OV2640 与 OV5640 在常见 24 针 DVP 模块上**引脚兼容**，
 *    下面这套映射实测可用（摄像头能被正常检测到）。
 * ⚠️ 当前板级代码**不初始化**摄像头，只是把映射记录下来备用。
 *
 * ✅ 2026-10-07：以下 16 项**已逐条比对板级原理图确认**
 *    （`hello_world/docs/modules/ESP32-S3CAM原理图.pdf`，第 1 页）
 *    不再是"从别处抄来的推测"。
 *
 * ⚠️ 命名陷阱（**核对时最容易错的地方**）：
 *    原理图的网络名是 **`CAM_Y2` ~ `CAM_Y9`**，而传感器侧标的是 `OV_D0` ~ `OV_D7`
 *    （原理图按 OV2640 画的，板上实装 OV5640 —— 引脚兼容，映射照旧成立）。
 *    对应关系是 **Y2↔D0、Y3↔D1、…、Y9↔D7**（下标差 2，不是从 Y0 起）。
 *    若按"Y0↔D0"去对，会**整体错位 2 位** —— 症状是图像花屏，且很难联想到是这里。
 *
 * ⚠️ 另外两处**与常见接线不同**，都是原理图确认的：
 *    - `OV_PWDN` 经 **R11(1K) 下拉到 GND**，**不接任何 GPIO** → 摄像头**常使能**
 *      → 故 `CAMERA_PIN_PWDN = NC` 是对的，不是"还没接"
 *    - `OV_RESET` 接的是 **`EN` 网络**（板子复位），**没有 GPIO 控制**
 *      → 故 `CAMERA_PIN_RESET = NC` 也是对的
 *
 * 顺带：原理图把 VSYNC 拼成了 `CAM_VYSNC`（字母顺序错了）—— 是**原图笔误**，
 *       别去"修正"引用它的代码。 */
#define CAMERA_PIN_D0           GPIO_NUM_11
#define CAMERA_PIN_D1           GPIO_NUM_9
#define CAMERA_PIN_D2           GPIO_NUM_8
#define CAMERA_PIN_D3           GPIO_NUM_10
#define CAMERA_PIN_D4           GPIO_NUM_12
#define CAMERA_PIN_D5           GPIO_NUM_18
#define CAMERA_PIN_D6           GPIO_NUM_17
#define CAMERA_PIN_D7           GPIO_NUM_16
#define CAMERA_PIN_XCLK         GPIO_NUM_15
#define CAMERA_PIN_PCLK         GPIO_NUM_13
#define CAMERA_PIN_VSYNC        GPIO_NUM_6
#define CAMERA_PIN_HREF         GPIO_NUM_7
#define CAMERA_PIN_SIOC         GPIO_NUM_5
#define CAMERA_PIN_SIOD         GPIO_NUM_4
#define CAMERA_PIN_PWDN         GPIO_NUM_NC
#define CAMERA_PIN_RESET        GPIO_NUM_NC
#define XCLK_FREQ_HZ            20000000

/* 摄像头实时预览的画面边长（正方形，单位像素）。
 * 240 = 垂直正好铺满屏高 240，水平居中后两侧各留 (284−240)/2 = 22px 黑边。
 * 改这个值要同步确认 camera_frame_center_offset() 仍能算出合法偏移。 */
#define CAMERA_PREVIEW_SIZE     240

#endif // _BOARD_CONFIG_H_
