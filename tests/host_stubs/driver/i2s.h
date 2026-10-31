#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
typedef int esp_err_t;
#define ESP_OK 0
#define I2S_NUM_0 0
#define I2S_MODE_MASTER 1
#define I2S_MODE_TX 2
typedef int i2s_mode_t;
#define I2S_BITS_PER_SAMPLE_16BIT 16
#define I2S_CHANNEL_FMT_RIGHT_LEFT 0
#define I2S_COMM_FORMAT_STAND_I2S 0
#define I2S_PIN_NO_CHANGE -1
#define pdMS_TO_TICKS(x) (x)
typedef struct {i2s_mode_t mode;int sample_rate;int bits_per_sample;int channel_format;int communication_format;int intr_alloc_flags;int dma_buf_count;int dma_buf_len;bool use_apll;bool tx_desc_auto_clear;int fixed_mclk;} i2s_config_t;
typedef struct {int mck_io_num;int bck_io_num;int ws_io_num;int data_out_num;int data_in_num;} i2s_pin_config_t;
static inline esp_err_t i2s_driver_install(int,const i2s_config_t*,int,void*){return 0;}
static inline esp_err_t i2s_driver_uninstall(int){return 0;}
static inline esp_err_t i2s_set_pin(int,const i2s_pin_config_t*){return 0;}
static inline esp_err_t i2s_write(int,const void*,size_t n,size_t*wrote,uint32_t){*wrote=n;return 0;}
static inline esp_err_t i2s_zero_dma_buffer(int){return 0;}
