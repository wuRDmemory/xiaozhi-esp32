#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

/* ==================================================================
 * ESP32-S3-CAM (AI-Thinker 映射, N16R8) + ICS-43434 数字麦
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
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_47   /* 麦克风 SD → ESP32 */

/* ------------------------------------------------------------------
 * 扬声器 —— ⚠️ 硬件尚未到位，这三个是**预留值**，还没在真机上验证过
 *
 * 选 41/42/43 的理由（用户确认排针引出 GPIO3~21 与 GPIO39~48）：
 *   41/42 = JTAG，不用外接调试器时可自由使用（console 走 USB-Serial-JTAG，
 *           不受影响）
 *   43    = UART0 TX；本板 console 走原生 USB，UART0 空闲
 *
 * 已避开的：39/40(SD)、45/46(strapping)、48(板载 LED)、19/20(USB D±)、
 *          4-13/15-18(摄像头)、26-37(Flash/八线 PSRAM)
 *
 * 将来接 MAX98357A 等 I²S 功放时，按 BCLK/LRCK/DIN 对应接这三根。
 * ------------------------------------------------------------------ */
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_41
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_42
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_43

/* 板载 BOOT 按键。
 * ✅ 2026-10-04 实测确认：按下触发 idle -> connecting，
 *    且该次状态迁移【没有】伴随 `Wake word detected` 日志 ——
 *    证明走的是按键路径而不是唤醒词路径。 */
#define BOOT_BUTTON_GPIO        GPIO_NUM_0

/* 摄像头（OV2640，AI-Thinker 映射）—— 第二步才用，这里先把引脚定义好。
 * ⚠️ 当前板级代码**不初始化**摄像头，只是把映射记录下来备用。 */
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

#endif // _BOARD_CONFIG_H_
