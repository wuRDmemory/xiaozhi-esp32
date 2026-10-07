#ifndef _CAMERA_FRAME_H_
#define _CAMERA_FRAME_H_

#include <stddef.h>
#include <stdint.h>

/* 摄像头帧的纯逻辑处理 —— 不依赖 ESP-IDF，可在 PC 上用 gcc 单测
 * （测试在 test/host/test_camera_frame.c）。
 *
 * ⚠️ `extern "C"` 是**必需**的，不是装饰：本头文件是 C 的，但调用方
 *    camera_preview.cc 是 C++。没有它，C++ 会把函数名 mangle 成
 *    `_Z26camera_frame_center_offsetii`，而 camera_frame.c 里导出的是未修饰名
 *    → **固件链接时报 undefined reference**。
 *    （主机单测是纯 C，测不出这个问题 —— 只在链接固件时暴露。） */
#ifdef __cplusplus
extern "C" {
#endif

/* 计算画面在屏上居中时，左侧黑边的宽度。
 * 装不下（或参数非法）返回 -1 —— 调用方必须挡住这种情况。 */
int camera_frame_center_offset(int panel_width, int frame_width);

/* RGB565 逐像素字节交换。
 *
 * ⚠️ **为什么必需**：摄像头（本板实测为 OV5640）输出的 RGB565 字节序
 *    与显示屏期望的不一致。
 *    依据是上游 esp32_camera.cc —— 它对每一帧都做 __builtin_bswap16，
 *    且 swap_bytes_enabled_ 默认为 true。详见 spec §7.1。
 *
 * 原地交换安全（dst == src 合法）。 */
void camera_frame_swap_bytes(uint16_t *dst, const uint16_t *src, size_t pixel_count);

#ifdef __cplusplus
}
#endif

#endif /* _CAMERA_FRAME_H_ */
