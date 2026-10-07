#include "camera_preview.h"
#include "camera_frame.h"
#include "config.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_camera.h>
#include <esp_lvgl_port.h>

#define TAG "CameraPreview"

CameraPreview::CameraPreview(esp_lcd_panel_handle_t panel, int panel_width, int frame_width)
    : panel_(panel), panel_width_(panel_width), frame_width_(frame_width) {
    x_offset_ = camera_frame_center_offset(panel_width_, frame_width_);
    if (x_offset_ < 0) {
        ESP_LOGE(TAG, "预览宽 %d 装不进屏宽 %d", frame_width_, panel_width_);
    }
}

CameraPreview::~CameraPreview() {
    Stop();
}

bool CameraPreview::Start() {
    if (running_) {
        return true;
    }
    if (x_offset_ < 0) {
        return false;                      /* 构造函数已报过错 */
    }

    camera_config_t cfg = {};
    cfg.ledc_channel  = LEDC_CHANNEL_0;    /* S3 的 XCLK 不走 LEDC，但字段必须填 */
    cfg.ledc_timer    = LEDC_TIMER_0;
    cfg.pin_d0        = CAMERA_PIN_D0;
    cfg.pin_d1        = CAMERA_PIN_D1;
    cfg.pin_d2        = CAMERA_PIN_D2;
    cfg.pin_d3        = CAMERA_PIN_D3;
    cfg.pin_d4        = CAMERA_PIN_D4;
    cfg.pin_d5        = CAMERA_PIN_D5;
    cfg.pin_d6        = CAMERA_PIN_D6;
    cfg.pin_d7        = CAMERA_PIN_D7;
    cfg.pin_xclk      = CAMERA_PIN_XCLK;
    cfg.pin_pclk      = CAMERA_PIN_PCLK;
    cfg.pin_vsync     = CAMERA_PIN_VSYNC;
    cfg.pin_href      = CAMERA_PIN_HREF;
    /* ⚠️ 不指定 sccb_i2c_port → 让 esp32-camera 自建 I2C，
     *    避免和板子上已有的 I2C 总线冲突 */
    cfg.pin_sccb_sda  = CAMERA_PIN_SIOD;
    cfg.pin_sccb_scl  = CAMERA_PIN_SIOC;
    cfg.pin_pwdn      = CAMERA_PIN_PWDN;   /* NC：原理图确认经 1K 下拉，摄像头常使能 */
    cfg.pin_reset     = CAMERA_PIN_RESET;  /* NC：接 EN 网络，无 GPIO 控制 */
    cfg.xclk_freq_hz  = XCLK_FREQ_HZ;
    cfg.pixel_format  = PIXFORMAT_RGB565;  /* 免解码，直接上屏 */
    cfg.frame_size    = FRAMESIZE_240X240; /* 垂直正好 240，水平居中留黑边 */
    cfg.jpeg_quality  = 12;                /* RGB565 下无用，但字段要填 */
    cfg.fb_count      = 2;                 /* 双缓冲 */
    cfg.fb_location   = CAMERA_FB_IN_PSRAM;
    cfg.grab_mode     = CAMERA_GRAB_WHEN_EMPTY;

    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init 失败: %s", esp_err_to_name(err));
        return false;
    }

    const size_t bytes = (size_t)frame_width_ * frame_width_ * sizeof(uint16_t);
    swap_buf_ = (uint8_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (swap_buf_ == nullptr) {
        ESP_LOGE(TAG, "帧缓冲分配失败 (%u 字节)", (unsigned)bytes);
        esp_camera_deinit();
        return false;
    }

    img_dsc_.header.cf     = LV_COLOR_FORMAT_RGB565;
    img_dsc_.header.w      = frame_width_;
    img_dsc_.header.h      = frame_width_;
    img_dsc_.header.stride = frame_width_ * 2;
    img_dsc_.data          = swap_buf_;
    img_dsc_.data_size     = bytes;

    if (!lvgl_port_lock(1000)) {
        ESP_LOGE(TAG, "取 LVGL 锁失败");
        heap_caps_free(swap_buf_);
        swap_buf_ = nullptr;
        esp_camera_deinit();
        return false;
    }
    /* 全屏黑底：补两侧黑边（不加它，黑边处会透出底下的 xiaozhi UI） */
    black_bg_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(black_bg_);
    lv_obj_set_size(black_bg_, panel_width_, frame_width_);
    lv_obj_set_style_bg_color(black_bg_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(black_bg_, LV_OPA_COVER, 0);
    lv_obj_center(black_bg_);

    lv_image_ = lv_image_create(lv_layer_top());
    lv_image_set_src(lv_image_, &img_dsc_);
    lv_obj_center(lv_image_);
    lvgl_port_unlock();

    running_ = true;
    xTaskCreate(TaskEntry, "cam_preview", 4096, this, 4, &task_);
    ESP_LOGI(TAG, "预览已启动：%dx%d，左侧黑边 %d，缓冲 %u 字节",
             frame_width_, frame_width_, x_offset_, (unsigned)bytes);
    return true;
}

void CameraPreview::Stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    /* 等预览任务自己退出（Run() 的循环会检查 running_ 并清空 task_） */
    while (task_ != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (lvgl_port_lock(1000)) {
        if (lv_image_ != nullptr) {
            lv_obj_delete(lv_image_);
            lv_image_ = nullptr;
        }
        if (black_bg_ != nullptr) {
            lv_obj_delete(black_bg_);
            black_bg_ = nullptr;
        }
        lvgl_port_unlock();
    }

    if (swap_buf_ != nullptr) {
        heap_caps_free(swap_buf_);
        swap_buf_ = nullptr;
    }
    esp_camera_deinit();
    ESP_LOGI(TAG, "预览已停止，摄像头已释放");
}

void CameraPreview::TaskEntry(void *arg) {
    static_cast<CameraPreview *>(arg)->Run();
}

void CameraPreview::Run() {
    const size_t pixels = (size_t)frame_width_ * frame_width_;
    uint32_t frames = 0;

    while (running_) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb == nullptr) {
            ESP_LOGW(TAG, "取帧失败");
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        /* ⚠️ 字节序交换是必需的，不是可选优化 —— 见 spec §7.1 */
        camera_frame_swap_bytes((uint16_t *)swap_buf_, (const uint16_t *)fb->buf, pixels);
        esp_camera_fb_return(fb);

        /* 只在这一小段持锁：把 image 标脏即可，渲染与 SPI 同步都交给 LVGL。
         * ⚠️ 不要在这里自己调 esp_lcd_panel_draw_bitmap —— 完成回调被
         *    esp_lvgl_port 占着，见 spec 修订记录 R2。 */
        if (lvgl_port_lock(100)) {
            lv_obj_invalidate(lv_image_);
            lvgl_port_unlock();
        }

        frames++;
        if ((frames % 100) == 0) {
            ESP_LOGI(TAG, "已推 %u 帧", (unsigned)frames);
        }
        /* 不额外延时：节奏由取帧阻塞与 LVGL 渲染自然决定 */
    }

    ESP_LOGI(TAG, "推帧任务退出，共 %u 帧", (unsigned)frames);
    task_ = nullptr;
    vTaskDelete(nullptr);
}
