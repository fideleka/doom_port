#pragma once
#include <lilka.h>
using esp_err_t = int;
constexpr int ESP_OK = 0;
constexpr int ESP_ERR_TIMEOUT = 1;
namespace esp_i2s {
enum i2s_mode_t { I2S_MODE_MASTER = 1, I2S_MODE_TX = 2 };
enum i2s_comm_format_t { I2S_COMM_FORMAT_STAND_I2S = 1 };
constexpr int I2S_NUM_0 = 0, I2S_BITS_PER_SAMPLE_16BIT = 16, I2S_CHANNEL_FMT_ONLY_LEFT = 1;
struct i2s_config_t {
    i2s_mode_t mode;
    unsigned sample_rate;
    int bits_per_sample, channel_format;
    i2s_comm_format_t communication_format;
    int intr_alloc_flags, dma_buf_count, dma_buf_len;
    bool tx_desc_auto_clear;
};
int i2s_driver_install(int, const i2s_config_t*, int, void*);
int i2s_driver_uninstall(int);
int i2s_zero_dma_buffer(int);
int i2s_write(int, const void*, size_t, size_t*, unsigned);
}
