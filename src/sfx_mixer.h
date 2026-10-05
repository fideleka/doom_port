#pragma once
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

namespace doom_audio {
constexpr int kVoices = 16; // Doom defaults to eight; reject larger engine channel indices safely.
constexpr uint32_t kRate = 11025;
constexpr size_t kBlock = 128;
inline int clamp(int value, int maximum) {
    return value < 0 ? 0 : (value > maximum ? maximum : value);
}
struct Sample {
    const uint8_t* data = nullptr;
    uint32_t length = 0;
    uint32_t rate = 0;
    Sample(const uint8_t* bytes = nullptr, uint32_t count = 0, uint32_t frequency = 0):
        data(bytes), length(count), rate(frequency) {
    }
};
// DMX: eight-byte header, then declared sample bytes including 16-byte guards at each end.
inline bool parseDMX(const uint8_t* data, size_t size, Sample& sample) {
    sample = {};
    if (!data || size < 8 || data[0] != 3 || data[1] != 0) return false;
    uint32_t rate = uint32_t(data[2]) | (uint32_t(data[3]) << 8);
    uint32_t length = uint32_t(data[4]) | (uint32_t(data[5]) << 8) | (uint32_t(data[6]) << 16) |
                      (uint32_t(data[7]) << 24);
    if (!rate || length <= 48 || length > size - 8) return false;
    sample = {data + 8 + 16, length - 32, rate};
    return true;
}
struct Voice {
    Sample sample;
    uint32_t position = 0;
    uint32_t phase = 0;
    int volume = 0;
    int handle = -1;
    int lump = -1; // Retained even after completion until the engine thread reaps it.
    bool active = false;
};
class Mixer {
public:
    Voice voices[kVoices];
    int start(int channel, Sample sample, int volume, int lump) {
        if (channel < 0 || channel >= kVoices || !sample.data || !sample.length || !sample.rate || sample.rate > 65535) return -1;
        // Never wrap handles: exhaustion after INT_MAX starts fails rather than reviving stale handles.
        if (nextHandle == INT_MAX) return -1;
        Voice& voice = voices[channel];
        voice = Voice{};
        voice.sample = sample;
        voice.volume = clamp(volume, 127);
        voice.handle = nextHandle++;
        voice.lump = lump;
        voice.active = true;
        return voice.handle;
    }
    Voice* find(int handle) {
        for (auto& voice : voices) if (handle >= 0 && voice.handle == handle) return &voice;
        return nullptr;
    }
    bool playing(int handle) {
        Voice* voice = find(handle);
        return voice && voice->active;
    }
    void stop(int handle) {
        Voice* voice = find(handle);
        if (voice) voice->active = false;
    }
    void update(int handle, int volume) {
        Voice* voice = find(handle);
        if (voice) voice->volume = clamp(volume, 127);
    }
    // Caller serializes with start/stop/update. No zone operations on the audio task.
    void render(int32_t* output, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            int32_t sum = 0;
            for (auto& voice : voices) {
                if (!voice.active) continue;
                sum += (int32_t(voice.sample.data[voice.position]) - 128) * 256 * voice.volume / 127;
                // phase < kRate and rate <= 65535 for DMX; no sample-count multiplication/overflow.
                voice.phase += voice.sample.rate;
                uint32_t step = voice.phase / kRate;
                voice.phase %= kRate;
                if (step >= voice.sample.length - voice.position) voice.active = false;
                else voice.position += step;
            }
            output[i] = sum;
        }
    }
    static void output(const int32_t* mixed, int16_t* pcm, size_t count, int master) {
        master = clamp(master, 100);
        for (size_t i = 0; i < count; ++i) {
            int32_t sample = mixed[i] * master / 100;
            pcm[i] = sample < -32768 ? -32768 : (sample > 32767 ? 32767 : sample);
        }
    }
private:
    int nextHandle = 0;
};
} // namespace doom_audio
