#include "wifi_board.h"
#include "ics43434_codec.h"
#include "application.h"
#include "button.h"
#include "config.h"

#include <esp_log.h>

#define TAG "S3CamIcs43434"

/*
 * ESP32-S3-CAM (AI-Thinker 映射, N16R8) + ICS-43434
 *
 * 这是一块**自定义板**，不是上游任何板子的变体 —— 见 config.h 顶部说明。
 *
 * 与上游 bread-compact-wifi-s3cam 的差别：
 *   - 麦克风接 14/21/47（那块接的是 1/2/42）
 *   - 不挂 LCD（那块挂了 ST7789，占用 19/20/38/45/47/21，会和这里的麦克风撞）
 *   - 音频 codec 换成 Ics43434Codec（64 SCK/帧 + >>16）
 *
 * 显示器与 LED 都用 Board 基类的默认实现（NoDisplay / NoLed），
 * 因此这里不覆盖 GetDisplay() / GetLed()，也就不会抢占引脚。
 * 摄像头暂未启用 —— 引脚映射已记在 config.h，留待第二步。
 */
class Esp32S3CamIcs43434Board : public WifiBoard {
private:
    Button boot_button_;

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            /* 启动阶段按一下 = 进入配网模式；之后按 = 切换对话状态 */
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

public:
    Esp32S3CamIcs43434Board() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeButtons();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Ics43434Codec audio_codec(
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }
};

DECLARE_BOARD(Esp32S3CamIcs43434Board);
