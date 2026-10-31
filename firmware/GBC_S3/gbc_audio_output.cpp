#include "gbc_audio_output.h"
#include <Arduino.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>
#include <atomic>

namespace GbcAudioOutput {
struct Packet { uint16_t count; int16_t mono[MAX_SAMPLES]; };
static QueueHandle_t queue = nullptr;
static TaskHandle_t worker = nullptr;
static bool ready = false;
static std::atomic<bool> active{true};
static std::atomic<bool> isPaused{true};
static volatile int gain = 35;

static void transmit(void *) {
  Packet packet;
  static int16_t stereo[MAX_SAMPLES * 2];
  for (;;) {
    if (xQueueReceive(queue, &packet, portMAX_DELAY) != pdTRUE) continue;
    if (isPaused || !active || packet.count == 0) continue;
    const int v = __atomic_load_n(&gain, __ATOMIC_RELAXED);
    for (unsigned i = 0; i < packet.count; ++i) {
      int32_t sample = ((int32_t)packet.mono[i] * v) / 100;
      if (sample > 32767) sample = 32767;
      else if (sample < -32768) sample = -32768;
      stereo[2 * i] = (int16_t)sample;
      stereo[2 * i + 1] = (int16_t)sample;
    }
    size_t wrote = 0;
    const size_t bytes = (size_t)packet.count * 2U * sizeof(int16_t);
    const esp_err_t result = i2s_write(I2S_NUM_0, stereo, bytes, &wrote, pdMS_TO_TICKS(50));
    static bool ioErrorReported=false;
    const bool failed=(result != ESP_OK || wrote != bytes);
    if(failed&&!ioErrorReported)Serial.printf("[GBC][AUDIO][IO_FAIL] err=%d bytes=%u/%u\n",(int)result,(unsigned)wrote,(unsigned)bytes);
    ioErrorReported=failed;
  }
}

bool begin() {
  if (ready) return true;
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = 0;
  cfg.dma_buf_count = 6;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = 0;
  esp_err_t result = i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
  if (result != ESP_OK) { Serial.printf("[GBC][AUDIO][FAIL] I2S install err=%d\n", (int)result); return false; }
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = BCLK; pins.ws_io_num = LRC;
  pins.data_out_num = DIN; pins.data_in_num = I2S_PIN_NO_CHANGE;
  result = i2s_set_pin(I2S_NUM_0, &pins);
  if (result != ESP_OK) { i2s_driver_uninstall(I2S_NUM_0); return false; }
  queue = xQueueCreate(8, sizeof(Packet));
  if (!queue) { i2s_driver_uninstall(I2S_NUM_0); return false; }
  if (xTaskCreatePinnedToCore(transmit, "GBC_I2S", 4096, nullptr, 2, &worker, 1) != pdPASS) {
    vQueueDelete(queue); queue = nullptr; i2s_driver_uninstall(I2S_NUM_0); return false;
  }
  ready = true;
  Serial.printf("[GBC][AUDIO][I2S] MAX98357A BCLK=%d LRC=%d DIN=%d rate=%d GNUBoy PCM unsigned-8 -> signed-16\n", BCLK, LRC, DIN, RATE);
  return true;
}

void pause(bool value) {
  isPaused = value;
  if (value && queue) xQueueReset(queue);
  if (value && ready) i2s_zero_dma_buffer(I2S_NUM_0);
}
void setEnabled(bool value) { active = value; if (!value) pause(true); }
bool enabled() { return active; }
bool initialized() { return ready; }
void setVolume(int percent) { if (percent < 0) percent = 0; if (percent > 100) percent = 100; __atomic_store_n(&gain, percent, __ATOMIC_RELAXED); }
int volume() { return __atomic_load_n(&gain, __ATOMIC_RELAXED); }

bool submitPcm(const uint8_t *samples, size_t count) {
  if (!samples || !count || !ready || !active || isPaused || !queue) return true;
  size_t offset = 0;
  while (offset < count) {
    Packet packet = {};
    size_t n = count - offset;
    if (n > MAX_SAMPLES) n = MAX_SAMPLES;
    packet.count = (uint16_t)n;
    for (size_t i = 0; i < n; ++i) {
      const int32_t centered = (int32_t)samples[offset + i] - 128;
      packet.mono[i] = (int16_t)(centered * 256);
    }
    if (xQueueSend(queue, &packet, 0) != pdTRUE) {
      Packet discarded;
      xQueueReceive(queue, &discarded, 0);
      if (xQueueSend(queue, &packet, 0) != pdTRUE) {

        offset += n;
        continue;
      }

    }

    offset += n;
  }
  return true;
}
} // namespace GbcAudioOutput

extern "C" int gbc_audio_submit_pcm(const unsigned char *samples, size_t count) {
  return GbcAudioOutput::submitPcm(samples, count) ? 1 : 0;
}
