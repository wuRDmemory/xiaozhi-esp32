#ifndef _ICS43434_CODEC_H_
#define _ICS43434_CODEC_H_

#include "audio_codec.h"

#include <driver/gpio.h>
#include <mutex>
#include <vector>

/*
 * ICS-43434 专用 codec。
 *
 * 为什么不能用上游的 NoAudioCodecSimplex：
 *   ICS-43434 要求**每 WS 帧正好 64 个 SCK**。上游把麦克风 I²S 配成
 *   `I2S_SLOT_MODE_MONO` + 32bit 槽 = **32 SCK/帧**，麦克风直接不工作
 *   （不是"音质差"，是根本没数据）。本类改成 STEREO + 32bit = 64 SCK/帧。
 *   上游的 `slot_mode` 是写死在构造函数里的，没法从外部覆盖，所以这里
 *   整份重写而不是继承。
 *
 * 另一处差异：上游 Read() 用 `>>12`（按 INMP441 调的音量增益），
 * ICS-43434 的 24bit 数据在 32bit 字的 [31:8]，正确取法是 `>>16`。
 *
 * 扬声器侧与上游一致（MAX98357A 这类 I²S 功放按 MONO/32bit 收就行）。
 */
class Ics43434Codec : public AudioCodec {
public:
    Ics43434Codec(int input_sample_rate, int output_sample_rate,
                  gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout,
                  gpio_num_t mic_sck, gpio_num_t mic_ws, gpio_num_t mic_din);
    virtual ~Ics43434Codec();

protected:
    int Write(const int16_t* data, int samples) override;
    int Read(int16_t* dest, int samples) override;
    void EnableInput(bool enable) override;
    void EnableOutput(bool enable) override;

private:
    std::mutex data_if_mutex_;
    /* 麦克风立体声原始数据（每帧 2 槽 × 4 字节）。成员变量而非每次分配，
     * 避免在音频路径上反复申请内存。 */
    std::vector<uint8_t> mic_raw_;
};

#endif // _ICS43434_CODEC_H_
