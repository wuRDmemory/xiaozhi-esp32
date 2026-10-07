#include "ics43434_codec.h"

#include <esp_log.h>
#include <cmath>
#include <cstring>
#include <driver/i2s_std.h>

#define TAG "Ics43434Codec"

/* 麦克风侧 DMA：与 voice_notes 阶段 1 已验证的配置一致（4 描述符 × 256 帧，
 * 每帧 8 字节 = 2 槽 × 32bit）。**不要改**，这套数值是实测通过的。 */
#define ICS43434_MIC_DMA_DESC_NUM   4
#define ICS43434_MIC_DMA_FRAME_NUM  256
/* 每帧字节数：立体声 2 槽 × 32bit */
#define ICS43434_BYTES_PER_FRAME    8
/* 24bit 数据在 32bit 字的 [31:8] → 右移 16 得 16bit */
#define ICS43434_SHIFT              16

Ics43434Codec::Ics43434Codec(int input_sample_rate, int output_sample_rate,
                             gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout,
                             gpio_num_t mic_sck, gpio_num_t mic_ws, gpio_num_t mic_din) {
    duplex_ = false;
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;

    /* ---------------- 扬声器：I²S 口 0，只发不收 ----------------
     *
     * ⚠️ 引脚为 NC 时**整个跳过**。本板 41/42/43 已让给 ST7789 屏幕，
     *    若照旧初始化，I²S 外设会去驱动屏幕的 RST/DC/CS —— 屏幕直接花掉，
     *    而且症状看起来像"屏幕坏了"，极难联想到是音频 codec 干的。
     *    所以这里必须是显式分支，不能只把引脚写成 NC 就完事。 */
    if (spk_bclk == GPIO_NUM_NC || spk_ws == GPIO_NUM_NC || spk_dout == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "扬声器引脚为 NC —— 跳过 TX 通道（本板无音频输出，见 config.h）");
        tx_handle_ = nullptr;
    } else {
    i2s_chan_config_t spk_chan_cfg = {
        .id = (i2s_port_t)0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&spk_chan_cfg, &tx_handle_, nullptr));

    i2s_std_config_t spk_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
#ifdef I2S_HW_VERSION_2
            .ext_clk_freq_hz = 0,
#endif
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
            .ws_pol = false,
            .bit_shift = true,
#ifdef I2S_HW_VERSION_2
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
#endif
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = spk_bclk,
            .ws = spk_ws,
            .dout = spk_dout,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &spk_cfg));
    }   /* ← 上面这块是扬声器初始化，引脚为 NC 时整块跳过 */

    /* ---------------- 麦克风：I²S 口 1，只收不发 ----------------
     *
     * ⚠️ 这里与上游唯一但**致命**的差别：STEREO。
     *    I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(32BIT, STEREO) 会给出
     *    slot_mask = BOTH、ws_width = 32bit，即 64 SCK/帧 —— ICS-43434 的要求。
     *    换成 MONO 就是 32 SCK/帧，麦克风**完全不出数据**。 */
    i2s_chan_config_t mic_chan_cfg = {
        .id = (i2s_port_t)1,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = ICS43434_MIC_DMA_DESC_NUM,
        .dma_frame_num = ICS43434_MIC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&mic_chan_cfg, nullptr, &rx_handle_));

    i2s_std_config_t mic_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)input_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,      /* S3 无 APLL，见 board.h */
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
#ifdef I2S_HW_VERSION_2
            .ext_clk_freq_hz = 0,
#endif
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_32BIT,
                        I2S_SLOT_MODE_STEREO),   /* ⚠️ 必须 STEREO：64 SCK/帧 */
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,             /* ICS-43434 不需要 MCLK */
            .bclk = mic_sck,
            .ws = mic_ws,
            .dout = I2S_GPIO_UNUSED,
            .din = mic_din,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &mic_cfg));

    ESP_LOGI(TAG, "ICS-43434 codec ready: mic %d Hz (STEREO/32bit = 64 SCK/frame, >>%d), spk %d Hz",
             input_sample_rate_, ICS43434_SHIFT, output_sample_rate_);
}

Ics43434Codec::~Ics43434Codec() {
    if (rx_handle_) {
        i2s_channel_disable(rx_handle_);
        i2s_del_channel(rx_handle_);
    }
    if (tx_handle_) {
        i2s_channel_disable(tx_handle_);
        i2s_del_channel(tx_handle_);
    }
}

/* ------------------------------------------------------------------ */
/* 麦克风：取左声道，>>16 转 16bit                                      */
/*                                                                     */
/* samples 是「想要的 16bit 单声道样本数」。因为每帧 2 槽只取 1 个，      */
/* 所以需要 samples 帧 = samples × 8 字节。                            */
/*                                                                     */
/* ⚠️ 模块的 LR 脚接 GND → 数据出在**左**声道（32bit 字下标偶数）。      */
/*    若实测读到的全是静音，先怀疑这里，而不是麦克风坏了。              */
/* ------------------------------------------------------------------ */
int Ics43434Codec::Read(int16_t* dest, int samples) {
    if (samples <= 0) {
        return 0;
    }

    const size_t want = (size_t)samples * ICS43434_BYTES_PER_FRAME;
    if (mic_raw_.size() < want) {
        mic_raw_.resize(want);
    }

    size_t bytes_read = 0;
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    esp_err_t err = i2s_channel_read(rx_handle_, mic_raw_.data(), want, &bytes_read,
                                     pdMS_TO_TICKS(200));
    if (err != ESP_OK || bytes_read == 0) {
        return 0;
    }

    const int frames = (int)(bytes_read / ICS43434_BYTES_PER_FRAME);
    const int32_t* words = reinterpret_cast<const int32_t*>(mic_raw_.data());
    for (int i = 0; i < frames; i++) {
        dest[i] = (int16_t)(words[i * 2] >> ICS43434_SHIFT);
    }
    return frames;
}

/* ------------------------------------------------------------------ */
/* 扬声器：与上游 NoAudioCodec::Write 同构（音量曲线 + 削顶保护）        */
/* ------------------------------------------------------------------ */
int Ics43434Codec::Write(const int16_t* data, int samples) {
    if (samples <= 0) {
        return 0;
    }
    /* 没有扬声器时直接丢弃 —— xiaozhi 会照常播提示音/回复，我们不拦，
     * 只是无声。返回 samples 表示"已消费"，避免上层把它当失败重试。 */
    if (tx_handle_ == nullptr) {
        return samples;
    }

    std::vector<int32_t> buffer(samples);
    /* output_volume_: 0-100 → volume_factor: 0-65536 */
    int32_t volume_factor = (int32_t)(pow((double)output_volume_ / 100.0, 2) * 65536);
    for (int i = 0; i < samples; i++) {
        int64_t temp = int64_t(data[i]) * volume_factor;
        if (temp > INT32_MAX) {
            buffer[i] = INT32_MAX;
        } else if (temp < INT32_MIN) {
            buffer[i] = INT32_MIN;
        } else {
            buffer[i] = static_cast<int32_t>(temp);
        }
    }

    size_t bytes_written = 0;
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    ESP_ERROR_CHECK(i2s_channel_write(tx_handle_, buffer.data(), samples * sizeof(int32_t),
                                      &bytes_written, portMAX_DELAY));
    return (int)(bytes_written / sizeof(int32_t));
}

void Ics43434Codec::EnableInput(bool enable) {
    if (enable == input_enabled_) {
        return;
    }
    if (enable) {
        ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));
    } else {
        ESP_ERROR_CHECK(i2s_channel_disable(rx_handle_));
    }
    AudioCodec::EnableInput(enable);
}

void Ics43434Codec::EnableOutput(bool enable) {
    if (enable == output_enabled_) {
        return;
    }
    /* ⚠️ 必须挡在 i2s_channel_enable 之前：没有 TX 通道时传 nullptr 会崩。
     * 仍调用基类，保持 output_enabled_ 状态一致（上层会读它）。 */
    if (tx_handle_ != nullptr) {
        if (enable) {
            ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
        } else {
            ESP_ERROR_CHECK(i2s_channel_disable(tx_handle_));
        }
    }
    AudioCodec::EnableOutput(enable);
}
