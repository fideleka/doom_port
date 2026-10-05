#include <lilka.h>
#include <I2S.h>
#include "driver/i2s.h"
#include <atomic>
#include "sfx_mixer.h"

extern "C" {
#include "config.h"
#include "i_sound.h"
#include "deh_str.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"
}

namespace {
using namespace doom_audio;
boolean32 use_sfx_prefix;
Mixer mixer;
SemaphoreHandle_t mixerMutex = nullptr;
SemaphoreHandle_t taskExited = nullptr;
std::atomic<bool> running{false};
bool installed = false;
// Engine callbacks/lifecycle are on the Doom thread, like WAD/zone operations.
// The audio task only reads pinned bytes and marks voices completed under this mutex.
void reap() {
    for (auto& voice : mixer.voices) {
        if (voice.active || voice.lump < 0) continue;
        int lump = voice.lump;
        voice.lump = -1;
        voice.sample = {};
        bool retained = false;
        for (const auto& other : mixer.voices) if (other.lump == lump) retained = true;
        if (!retained) W_ReleaseLumpNum(lump);
    }
}
void soundTask(void*) {
    int32_t mixed[kBlock];
    int16_t pcm[kBlock];
    while (running.load()) {
        xSemaphoreTake(mixerMutex, portMAX_DELAY);
        mixer.render(mixed, kBlock);
        xSemaphoreGive(mixerMutex);
        size_t offset = 0;
        while (running.load() && offset < sizeof(pcm)) {
            // Live RAM-only SDK getter; scale the wide sum once, including partial retries.
            Mixer::output(mixed, pcm, kBlock, lilka::audio.getVolume());
            size_t written = 0;
            esp_err_t result = esp_i2s::i2s_write(
                esp_i2s::I2S_NUM_0, reinterpret_cast<uint8_t*>(pcm) + offset,
                sizeof(pcm) - offset, &written, pdMS_TO_TICKS(20) + 1
            );
            if (written > sizeof(pcm) - offset || written % sizeof(int16_t)) {
                // Broken driver result: fail closed, never advance to an unaligned sample.
                running.store(false);
                break;
            }
            offset += written;
            if (result != ESP_OK && result != ESP_ERR_TIMEOUT) {
                running.store(false);
                break;
            }
            // Driver backpressure paces normal output (including silence). No guessed sample sleep.
            if (!written) vTaskDelay(1);
        }
    }
    xSemaphoreGive(taskExited);
    vTaskDelete(nullptr);
}
void cleanup() {
    if (installed) {
        esp_i2s::i2s_driver_uninstall(esp_i2s::I2S_NUM_0);
        installed = false;
    }
    if (mixerMutex) {
        for (auto& voice : mixer.voices) voice.active = false;
        reap();
        vSemaphoreDelete(mixerMutex);
        mixerMutex = nullptr;
    }
    if (taskExited) {
        vSemaphoreDelete(taskExited);
        taskExited = nullptr;
    }
}
} // namespace

static snddevice_t sound_devices[] = {
    SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS, SNDDEVICE_WAVEBLASTER, SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32,
};
static boolean32 I_I2S_InitSound(boolean32 prefix) {
    if (mixerMutex) return running.load();
    use_sfx_prefix = prefix;
    // Force any initial NVS load onto the engine thread, never the output task.
    lilka::audio.getVolume();
    mixerMutex = xSemaphoreCreateMutex();
    taskExited = xSemaphoreCreateBinary();
    if (!mixerMutex || !taskExited) {
        cleanup();
        return false;
    }
    esp_i2s::i2s_config_t cfg = {};
    cfg.mode = static_cast<esp_i2s::i2s_mode_t>(esp_i2s::I2S_MODE_MASTER | esp_i2s::I2S_MODE_TX);
    cfg.sample_rate = kRate;
    cfg.bits_per_sample = esp_i2s::I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = esp_i2s::I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = esp_i2s::I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 2;
    cfg.dma_buf_len = kBlock;
    cfg.tx_desc_auto_clear = true;
    // SDK startup hello can own I2S0. Never uninstall/override a competing driver.
    if (esp_i2s::i2s_driver_install(esp_i2s::I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) {
        cleanup();
        lilka::serial_log("Doom SFX: I2S0 unavailable (possibly SDK startup sound)");
        return false;
    }
    installed = true;
    lilka::audio.initPins();
    if (esp_i2s::i2s_zero_dma_buffer(esp_i2s::I2S_NUM_0) != ESP_OK) {
        cleanup();
        return false;
    }
    running.store(true);
    if (xTaskCreatePinnedToCore(soundTask, "doomSfx", 3072, nullptr, 1, nullptr, 0) != pdPASS) {
        running.store(false);
        cleanup();
        return false;
    }
    return true;
}
static void I_I2S_ShutdownSound() {
    if (!mixerMutex) return;
    running.store(false);
    // Output has a bounded driver timeout and signals before deleting itself.
    xSemaphoreTake(taskExited, portMAX_DELAY);
    cleanup();
}
static int I_I2S_GetSfxLumpNum(sfxinfo_t* sfx) {
    char name[9];
    if (sfx->link) sfx = sfx->link;
    if (use_sfx_prefix) M_snprintf(name, sizeof(name), "ds%s", DEH_String(sfx->name));
    else M_StringCopy(name, DEH_String(sfx->name), sizeof(name));
    return W_CheckNumForName(name);
}
static void I_I2S_UpdateSound() {
    if (!mixerMutex) return;
    xSemaphoreTake(mixerMutex, portMAX_DELAY);
    reap();
    xSemaphoreGive(mixerMutex);
}
static void I_I2S_UpdateSoundParams(int handle, int vol, int sep) {
    (void)sep; // Physical mono: separation intentionally has no effect, not a stereo promise.
    if (!mixerMutex) return;
    xSemaphoreTake(mixerMutex, portMAX_DELAY);
    mixer.update(handle, vol);
    xSemaphoreGive(mixerMutex);
}
static int I_I2S_StartSound(sfxinfo_t* sfx, int channel, int vol, int sep) {
    (void)sep;
    if (!running.load() || channel < 0 || channel >= kVoices) return -1;
    int lump = I_I2S_GetSfxLumpNum(sfx);
    if (lump < 0) return -1;
    int size = W_LumpLength(lump);
    if (size < 8) return -1;
    // W_CacheLumpNum(PU_SOUND) pins non-mapped zone data; PU_CACHE can be purged.
    // All cache/release operations stay on the engine thread, not the audio task.
    const auto* data = static_cast<const uint8_t*>(W_CacheLumpNum(lump, PU_SOUND));
    Sample sample;
    bool valid = parseDMX(data, static_cast<size_t>(size), sample);
    xSemaphoreTake(mixerMutex, portMAX_DELAY);
    int handle = -1;
    if (valid) {
        mixer.voices[channel].active = false;
        // Avoid unpin/re-cache between replacing two voices using the same lump.
        int old = mixer.voices[channel].lump;
        mixer.voices[channel].lump = lump;
        if (old >= 0 && old != lump) {
            bool retained = false;
            for (const auto& voice : mixer.voices) if (voice.lump == old) retained = true;
            if (!retained) W_ReleaseLumpNum(old);
        }
        handle = mixer.start(channel, sample, vol, lump);
    }
    if (handle < 0) {
        bool retained = false;
        for (const auto& voice : mixer.voices) if (voice.lump == lump) retained = true;
        if (!retained) W_ReleaseLumpNum(lump);
    }
    reap();
    xSemaphoreGive(mixerMutex);
    return handle;
}
static void I_I2S_StopSound(int handle) {
    if (!mixerMutex) return;
    xSemaphoreTake(mixerMutex, portMAX_DELAY);
    mixer.stop(handle);
    reap();
    xSemaphoreGive(mixerMutex);
}
static boolean32 I_I2S_SoundIsPlaying(int handle) {
    if (!mixerMutex || !running.load()) return false;
    xSemaphoreTake(mixerMutex, portMAX_DELAY);
    bool playing = mixer.playing(handle);
    reap();
    xSemaphoreGive(mixerMutex);
    return playing;
}
static void I_I2S_PrecacheSounds(sfxinfo_t*, int) {
}
sound_module_t sound_module_I2S = {
    sound_devices, arrlen(sound_devices), I_I2S_InitSound, I_I2S_ShutdownSound, I_I2S_GetSfxLumpNum,
    I_I2S_UpdateSound, I_I2S_UpdateSoundParams, I_I2S_StartSound, I_I2S_StopSound, I_I2S_SoundIsPlaying,
    I_I2S_PrecacheSounds,
};
