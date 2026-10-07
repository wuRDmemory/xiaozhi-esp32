#pragma once

#include <lvgl.h>
#include <esp_lcd_panel_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/* 摄像头实时预览（全屏居中，两侧黑边）
 *
 * 设计见 hello_world/docs/plans/2026-10-07-xiaozhi-camera-preview.md
 *
 * 上屏路径：帧数据交给 LVGL 顶层的全屏 lv_image，SPI 完成同步由 LVGL 负责。
 * ⚠️ **不要**改成"绕过 LVGL 直推面板" —— 判断 DMA 完成的回调
 *    （on_color_trans_done）已被 esp_lvgl_port 占用，抢过来就永久搞坏 LVGL，
 *    而 esp_lcd 没有 getter 无法还原。详见 spec 修订记录 R2。
 */
class CameraPreview {
public:
    /* panel:      ST7789 的 panel 句柄
     * panel_width: 屏幕宽（284，横屏）
     * frame_width: 预览画面边长（240，正方形） */
    CameraPreview(esp_lcd_panel_handle_t panel, int panel_width, int frame_width);
    ~CameraPreview();

    /* 进入预览。摄像头初始化失败时返回 false，且不留任何副作用。 */
    bool Start();
    /* 退出预览：停任务 → 删 image → 释放摄像头。可重复调用。 */
    void Stop();
    bool IsRunning() const { return running_; }

private:
    static void TaskEntry(void *arg);
    void Run();

    esp_lcd_panel_handle_t panel_;
    int panel_width_;
    int frame_width_;
    int x_offset_ = -1;

    volatile bool running_ = false;
    TaskHandle_t task_ = nullptr;

    uint8_t *swap_buf_ = nullptr;      /* 交换字节序后的 RGB565 缓冲（PSRAM） */
    lv_obj_t *black_bg_ = nullptr;     /* 全屏黑底：补两侧黑边，否则黑边处透出 UI */
    lv_obj_t *lv_image_ = nullptr;     /* 全屏画面 */
    lv_image_dsc_t img_dsc_ = {};      /* ⚠️ 必须是成员：LVGL 会引用它，栈变量会悬空 */
};
