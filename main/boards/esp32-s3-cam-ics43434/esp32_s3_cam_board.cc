#include "wifi_board.h"
#include "ics43434_codec.h"
#include "camera_preview.h"
#include "display/lcd_display.h"
#include "backlight.h"
#include "application.h"
#include "button.h"
#include "config.h"

#include <esp_log.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

#define TAG "S3CamIcs43434"

/*
 * ESP32-S3-CAM (GOOUUU 板, N16R8) + ICS-43434 + ST7789 240×284
 *
 * 这是一块**自定义板**，不是上游任何板子的变体 —— 见 config.h 顶部说明。
 *
 * 与上游 bread-compact-wifi-s3cam 的差别：
 *   - 麦克风接 14/21/47（那块接的是 1/2/42）
 *   - 音频 codec 换成 Ics43434Codec（64 SCK/帧 + >>16）
 *   - 显示是 ST7789 SPI 屏，接 39~44（那块是 ST7789 接 19/20/38/45/47/21，
 *     会和这里的麦克风在 47、21 上撞车）
 *   - **没有扬声器**：41/42/43 已让给屏幕，见 config.h
 *
 * 摄像头暂未启用（引脚已由原理图实证，记在 config.h）。
 */
class Esp32S3CamIcs43434Board : public WifiBoard {
private:
    Button boot_button_;
    Display* display_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;   /* 预览模块要用，故存为成员 */
    CameraPreview* preview_ = nullptr;

    /* ST7789 走 SPI3。注意这几根脚全在 GPIO 交换矩阵上（不是 FSPI 的
     * IOMUX 脚，那些是 9~14 —— 已被摄像头占光），所以 SPI 时钟上限约
     * 40 MHz。对这个分辨率够用：240×284×2B ÷ 40MHz ≈ 3.4 ms/帧。 */
    static constexpr spi_host_device_t kDisplaySpiHost = SPI3_HOST;
    static constexpr int kDisplaySpiClockHz = 40 * 1000 * 1000;

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;   /* 屏只写不读 */
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(kDisplaySpiHost, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = kDisplaySpiClockHz;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(kDisplaySpiHost, &io_config, &panel_io));

        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        /* ⚠️ 这里**不要**再调 esp_lcd_panel_set_gap —— 偏移由下面
         *    SpiLcdDisplay 的构造参数处理，两处都设会【叠加两次】，
         *    画面会偏出屏幕。上游板子也都是只传构造参数。
         *    偏移值 DISPLAY_OFFSET_Y 是算出来的（(320−284)/2 = 18），
         *    画面上下错位时第一个该调的是它 —— 见 config.h。 */

        panel_ = panel;                  /* 存成员：预览模块上屏要用 */
        display_ = new SpiLcdDisplay(panel_io, panel,
                                     DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                     DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                     DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            /* ⚠️ 这条必须放在最前：预览中短按 = 退出预览（spec D4，防止卡在预览里出不来） */
            if (preview_ != nullptr && preview_->IsRunning()) {
                preview_->Stop();
                return;
            }
            auto& app = Application::GetInstance();
            /* 启动阶段按一下 = 进入配网模式；之后按 = 切换对话状态 */
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        /* 长按 = 切进/切出摄像头预览。
         * ⚠️ 短按语义**不变**（仍是触发对话）—— 见 spec D3 与回归判据 §9.5。 */
        boot_button_.OnLongPress([this]() {
            if (preview_ == nullptr) {
                return;
            }
            if (preview_->IsRunning()) {
                preview_->Stop();
            } else if (!preview_->Start()) {
                ESP_LOGE(TAG, "进入预览失败");
            }
        });
    }

public:
    Esp32S3CamIcs43434Board() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();

        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
        /* ⚠️ 两个偏移都要打 —— 横屏（swap_xy）后偏移会换到 X 轴，
         *    只打 offset_y 会显示成 0，误导后面的朝向调试。 */
        ESP_LOGI(TAG, "ST7789 %dx%d ready (SPI3, swap_xy=%d, offset %d,%d, mirror %d,%d)",
                 DISPLAY_WIDTH, DISPLAY_HEIGHT, (int)DISPLAY_SWAP_XY,
                 DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                 (int)DISPLAY_MIRROR_X, (int)DISPLAY_MIRROR_Y);

        /* 预览对象常驻，但**不在这里 Start** —— 由长按 BOOT 触发（见 InitializeButtons）。
         * 好处：开机不占摄像头与那 230KB 帧缓冲，进预览时才付这笔开销。 */
        preview_ = new CameraPreview(panel_, DISPLAY_WIDTH, CAMERA_PREVIEW_SIZE);
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Ics43434Codec audio_codec(
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }
};

DECLARE_BOARD(Esp32S3CamIcs43434Board);
