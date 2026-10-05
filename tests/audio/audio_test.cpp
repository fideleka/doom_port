#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <functional>
#include "../../src/i_i2ssound.cpp"

static std::thread::id engineThread = std::this_thread::get_id();
static std::vector<uint8_t> lumps[2];
static bool pinned[2] = {};
static int releases[2] = {};
static int selected = 0;
static bool installFail = false, zeroFail = false;
static int installs = 0, uninstalls = 0;
static std::function<int(const void*, size_t, size_t*)> writer;
extern "C" {
int W_CheckNumForName(char*) { return selected; }
int W_LumpLength(unsigned int lump) { return lumps[lump].size(); }
void* W_CacheLumpNum(int lump, int tag) {
    assert(std::this_thread::get_id() == engineThread && tag == PU_SOUND);
    pinned[lump] = true;
    return lumps[lump].data();
}
void W_ReleaseLumpNum(int lump) {
    assert(std::this_thread::get_id() == engineThread && pinned[lump]);
    pinned[lump] = false;
    ++releases[lump];
}
int M_snprintf(char* out, size_t size, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int result = vsnprintf(out, size, fmt, args);
    va_end(args);
    return result;
}
boolean32 M_StringCopy(char* out, const char* in, size_t size) {
    snprintf(out, size, "%s", in);
    return true;
}
}
namespace esp_i2s {
int i2s_driver_install(int, const i2s_config_t* cfg, int, void*) {
    assert(cfg->sample_rate == 11025 && cfg->dma_buf_len == 128 && cfg->dma_buf_count == 2);
    if (installFail) return -1;
    ++installs;
    return ESP_OK;
}
int i2s_driver_uninstall(int) { ++uninstalls; return ESP_OK; }
int i2s_zero_dma_buffer(int) { return zeroFail ? -1 : ESP_OK; }
int i2s_write(int, const void* data, size_t size, size_t* written, unsigned ticks) {
    assert(locksHeld == 0 && ticks > 0 && size <= 256 && size % 2 == 0);
    return writer(data, size, written);
}
}
static std::vector<uint8_t> dmx(size_t payload, uint16_t rate, uint8_t value) {
    std::vector<uint8_t> data(payload + 40, value);
    data[0] = 3; data[1] = 0;
    data[2] = rate & 255; data[3] = rate >> 8;
    uint32_t declared = payload + 32;
    for (int i = 0; i < 4; ++i) data[4 + i] = (declared >> (8 * i)) & 255;
    return data;
}
static void coreTests() {
    using namespace doom_audio;
    Sample sample;
    auto data = dmx(17, 11025, 129);
    assert(parseDMX(data.data(), data.size(), sample));
    assert(sample.data == data.data() + 24 && sample.length == 17);
    for (size_t size = 0; size < data.size(); ++size) assert(!parseDMX(data.data(), size, sample));
    data[0] = 4; assert(!parseDMX(data.data(), data.size(), sample)); data[0] = 3;
    data[2] = data[3] = 0; assert(!parseDMX(data.data(), data.size(), sample));
    data = dmx(16, 11025, 129); assert(!parseDMX(data.data(), data.size(), sample));
    data = dmx(17, 11025, 129); data[7] = 255; assert(!parseDMX(data.data(), data.size(), sample));
    assert(!parseDMX(nullptr, 100, sample));
    uint8_t plus[] = {129,129,129,129};
    uint8_t minus[] = {127,127,127,127};
    int32_t mix[16]; int16_t pcm[16];
    Mixer m;
    int a = m.start(0, {plus,4,11025},127,0);
    int b = m.start(1, {plus,4,11025},127,0);
    m.render(mix,1); assert(mix[0] == 512 && m.playing(a) && m.playing(b));
    m.update(b,0); m.render(mix,1); assert(mix[0] == 256);
    m.stop(a); m.render(mix,1); assert(mix[0] == 0 && !m.playing(a));
    int replacement = m.start(1,{minus,4,11025},127,0);
    m.stop(b); assert(m.playing(replacement) && !m.playing(b));
    m.render(mix,4); assert(mix[0] == -256 && mix[3] == -256 && !m.playing(replacement));
    m.render(mix,1); assert(mix[0] == 0);
    assert(m.start(-1,{plus,4,11025},127,0) == -1);
    assert(m.start(kVoices,{plus,4,11025},127,0) == -1);
    assert(m.start(0,{plus,4,65536},127,0) == -1);
    uint8_t hi[] = {255,255}; uint8_t lo[] = {0,0};
    for (int i = 0; i < kVoices; ++i) assert(m.start(i,{hi,2,11025},999,0) >= 0);
    m.render(mix,1); Mixer::output(mix,pcm,1,100); assert(pcm[0] == 32767);
    Mixer::output(mix,pcm,1,0); assert(pcm[0] == 0);
    Mixer::output(mix,pcm,1,1); assert(pcm[0] == mix[0]/100);
    for (int i = 0; i < kVoices; ++i) m.start(i,{lo,2,11025},127,0);
    m.render(mix,1); Mixer::output(mix,pcm,1,100); assert(pcm[0] == -32768);
    for (uint32_t rate : {1u,5512u,11025u,22050u,44100u,65535u}) {
        Mixer rates;
        uint8_t ramp[] = {129,130,131,132};
        int handle = rates.start(0,{ramp,4,rate},127,0);
        uint32_t frames = (4*11025 + rate - 1)/rate;
        for (uint32_t frame = 0; frame < frames; ++frame) {
            rates.render(mix,1);
            assert(mix[0] == int32_t(1 + frame*rate/11025)*256);
        }
        assert(!rates.playing(handle));
    }
    printf("core: DMX bounds, overlap, signed clipping, gain, rates, handles PASS; Mixer=%zu Voice=%zu\n", sizeof(Mixer), sizeof(Voice));
}
static void joinShutdown() {
    I_I2S_ShutdownSound();
    if (audioThread.joinable()) audioThread.join();
    assert(!mixerMutex && !taskExited && !installed && semaphoreCount == 0);
    assert(!pinned[0] && !pinned[1]);
}
static void backendTests() {
    for (int fail : {1,2}) { failSemaphore = fail; assert(!I_I2S_InitSound(true)); assert(semaphoreCount == 0); }
    installFail = true; assert(!I_I2S_InitSound(true)); installFail = false;
    assert(uninstalls == 0); // Never uninstall the SDK/other owner's driver.
    zeroFail = true; assert(!I_I2S_InitSound(true)); zeroFail = false;
    failTask = true; assert(!I_I2S_InitSound(true)); failTask = false;
    assert(installs == uninstalls && semaphoreCount == 0);
    // Hold first write to deterministically test engine calls while I2S is blocked.
    std::mutex gate; std::condition_variable cv; bool entered = false, proceed = false;
    int calls = 0;
    std::vector<int16_t> received;
    writer = [&](const void* data, size_t size, size_t* written) {
        if (calls++ == 0) {
            std::unique_lock<std::mutex> lock(gate); entered = true; cv.notify_one();
            cv.wait(lock,[&]{return proceed;});
            const auto* pcm = static_cast<const int16_t*>(data);
            for (size_t i=0;i<size/2;++i) assert(pcm[i] == 0); // Inactive silence fed.
            *written = size; return ESP_OK;
        }
        if (calls == 2) { *written = 0; return ESP_ERR_TIMEOUT; }
        const auto* pcm = static_cast<const int16_t*>(data);
        size_t count = size > 32 ? 32 : size;
        received.insert(received.end(),pcm,pcm+count/2);
        *written = count;
        if (calls == 3) lilka::Audio::volume.store(0);
        if (received.size() == 128) { running.store(false); }
        return ESP_OK;
    };
    lumps[0] = dmx(4096,11025,129); lumps[1] = dmx(4096,22050,130);
    assert(I_I2S_InitSound(true));
    { std::unique_lock<std::mutex> lock(gate); cv.wait(lock,[&]{return entered;}); }
    sfxinfo_t sfx = {}; strcpy(sfx.name,"test");
    int a = I_I2S_StartSound(&sfx,0,127,0);
    int b = I_I2S_StartSound(&sfx,1,127,255);
    assert(a >= 0 && b != a && pinned[0]);
    I_I2S_StopSound(a); assert(pinned[0] && !I_I2S_SoundIsPlaying(a));
    int c = I_I2S_StartSound(&sfx,1,127,128);
    I_I2S_StopSound(b); assert(I_I2S_SoundIsPlaying(c) && pinned[0]);
    selected = -1;
    assert(I_I2S_StartSound(&sfx, 3, 127, 128) == -1);
    selected = 1;
    lumps[1][0] = 4;
    assert(I_I2S_StartSound(&sfx, 3, 127, 128) == -1 && !pinned[1]);
    lumps[1][0] = 3;
    int d = I_I2S_StartSound(&sfx,2,127,128);
    I_I2S_UpdateSoundParams(d,0,0);
    assert(I_I2S_SoundIsPlaying(d));
    assert(I_I2S_StartSound(&sfx,16,127,0) == -1);
    { std::lock_guard<std::mutex> lock(gate); proceed=true; cv.notify_one(); }
    audioThread.join();
    assert(received.size() == 128 && received[0] == 256);
    for (size_t i=16;i<received.size();++i) assert(received[i] == 0);
    joinShutdown();
    // Hard/invalid I2S errors terminate output safely, then engine shutdown unpins.
    for (int mode : {0,1,2}) {
        writer = [mode](const void*, size_t size, size_t* written) {
            *written = mode == 0 ? 0 : (mode == 1 ? 1 : size+2);
            return mode == 0 ? -1 : ESP_OK;
        };
        assert(I_I2S_InitSound(true)); audioThread.join(); joinShutdown();
    }
    // Concurrent start/update/stop vs renderer and shutdown while write in progress.
    lilka::Audio::volume.store(100);
    writer = [](const void*,size_t size,size_t* written) { *written=size; std::this_thread::yield(); return ESP_OK; };
    assert(I_I2S_InitSound(true));
    for (int i=0;i<2000;++i) {
        selected = i%2; int h=I_I2S_StartSound(&sfx,i%16,127,128);
        I_I2S_UpdateSoundParams(h,i%128,255); I_I2S_SoundIsPlaying(h);
        if (i%3 == 0) I_I2S_StopSound(h);
        I_I2S_UpdateSound();
    }
    joinShutdown();
    assert(installs == uninstalls);
    printf("backend: pinning, restart/stale handles, live mute, partial/timeout/error, silence, init cleanup, concurrency/shutdown PASS\n");
}
int main() { coreTests(); backendTests(); }
