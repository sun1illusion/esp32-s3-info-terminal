#include "board_audio.h"

#include <math.h>
#include <string.h>
#include "driver/i2s_std.h"
#include "esp_log.h"

static const char *TAG = "board_audio";

/* I2S0 留给麦克风（4/5/6），喇叭独占 I2S1。
 * 两者时钟引脚不同，不能共用一个控制器。 */
#define AUDIO_I2S_PORT      I2S_NUM_1
#define AUDIO_BCLK_GPIO     15
#define AUDIO_LRC_GPIO      16
#define AUDIO_DOUT_GPIO     7     /* MAX98357A DIN */

/* 4 个描述符 × 256 帧 × 4 字节 ≈ 46ms @22.05kHz。
 * 流式播放时外部还有 PCM 缓冲和网络环形缓冲兜底，这里不宜开太大。 */
#define AUDIO_DMA_DESC_NUM  4
#define AUDIO_DMA_FRAME_NUM 256

static i2s_chan_handle_t s_tx = NULL;
static int s_volume = 70;

esp_err_t Audio_Init(void)
{
    if (s_tx) return ESP_OK;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = AUDIO_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = AUDIO_DMA_FRAME_NUM;
    /* 欠载时自动填 0：否则 DMA 会重复上一块数据，听起来像鬼畜循环 */
    chan_cfg.auto_clear   = true;

    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel 失败: %s", esp_err_to_name(err));
        s_tx = NULL;
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,     /* MAX98357A 内部自生成 MCLK */
            .bclk = AUDIO_BCLK_GPIO,
            .ws   = AUDIO_LRC_GPIO,
            .dout = AUDIO_DOUT_GPIO,
            .din  = I2S_GPIO_UNUSED,
        },
    };

    err = i2s_channel_init_std_mode(s_tx, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init_std_mode 失败: %s", esp_err_to_name(err));
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return err;
    }

    err = i2s_channel_enable(s_tx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "channel_enable 失败: %s", esp_err_to_name(err));
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return err;
    }

    ESP_LOGI(TAG, "音频输出就绪（%d Hz / 16bit / 立体声，DIN=%d BCLK=%d LRC=%d）",
             AUDIO_SAMPLE_RATE, AUDIO_DOUT_GPIO, AUDIO_BCLK_GPIO, AUDIO_LRC_GPIO);
    return ESP_OK;
}

void Audio_Deinit(void)
{
    if (!s_tx) return;
    i2s_channel_disable(s_tx);
    i2s_del_channel(s_tx);
    s_tx = NULL;
}

void Audio_SetVolume(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_volume = percent;
}

int Audio_GetVolume(void)
{
    return s_volume;
}

esp_err_t Audio_WritePCM(const int16_t *stereo, size_t frames, uint32_t timeout_ms)
{
    if (!s_tx) return ESP_ERR_INVALID_STATE;
    if (!stereo || frames == 0) return ESP_ERR_INVALID_ARG;

    /* 音量缩放要就地改数据，调用方的缓冲不能被污染，所以分块拷到本地暂存区。
     * 256 帧 × 2 声道 × 2 字节 = 1KB，栈上放得下。 */
    enum { CHUNK = 256 };
    int16_t tmp[CHUNK * 2];
    const int16_t *src = stereo;

    while (frames > 0) {
        size_t n = (frames > CHUNK) ? CHUNK : frames;

        for (size_t i = 0; i < n * 2; i++) {
            tmp[i] = (int16_t)((int32_t)src[i] * s_volume / 100);
        }

        size_t written = 0;
        esp_err_t err = i2s_channel_write(s_tx, tmp, n * 2 * sizeof(int16_t),
                                          &written, timeout_ms);
        if (err != ESP_OK) return err;

        src    += n * 2;
        frames -= n;
    }
    return ESP_OK;
}

esp_err_t Audio_PlayTone(int freq_hz, int duration_ms)
{
    if (!s_tx) return ESP_ERR_INVALID_STATE;
    if (freq_hz <= 0 || duration_ms <= 0) return ESP_ERR_INVALID_ARG;

    enum { FRAMES = 256 };
    static int16_t buf[FRAMES * 2];

    const int    total = (AUDIO_SAMPLE_RATE / 1000) * duration_ms;
    const double step  = 2.0 * M_PI * (double)freq_hz / (double)AUDIO_SAMPLE_RATE;
    double phase = 0.0;

    int done = 0;
    while (done < total) {
        int n = (total - done < FRAMES) ? (total - done) : FRAMES;
        for (int i = 0; i < n; i++) {
            /* 幅度压到 8000（满量程约 1/4），避免小喇叭失真和刺耳 */
            int16_t v = (int16_t)(sin(phase) * 8000.0
                                  * (double)s_volume / 100.0);
            buf[i * 2 + 0] = v;   /* 左 */
            buf[i * 2 + 1] = v;   /* 右：MAX98357A 取单路，两路写相同值 */
            phase += step;
            if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
        }

        size_t written = 0;
        esp_err_t err = i2s_channel_write(s_tx, buf, n * 2 * sizeof(int16_t),
                                          &written, 1000);
        if (err != ESP_OK) return err;

        done += n;
    }
    return ESP_OK;
}
