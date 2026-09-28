// Capture an actual menu-free Doom demo framebuffer for Lilka UI previews.
// Built only by tools/render_demo_preview.sh; not linked into firmware.
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include "doomgeneric.h"
#include "doomstat.h"
#include "d_main.h"
#include "d_alloc.h"
#include "i_sound.h"

static const char* output_path;
static int requested_frame = 150;
static int demo_frames;
static int capture_wipe;
static uint32_t clock_start;

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull);
}
void DG_Init(void) { clock_start = now_ms(); }
uint32_t DG_GetTicksMs(void) { return now_ms() - clock_start; }
uint32_t DG_GetTicksUs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000000ull + ts.tv_nsec / 1000ull);
}
void DG_PerfFrame(uint32_t simulation_us, uint32_t display_us) {
    (void)simulation_us; (void)display_us;
}
void DG_PerfConvert(uint32_t conversion_us) { (void)conversion_us; }
void DG_PerfWorld(uint32_t world_us) { (void)world_us; }
void DG_SleepMs(uint32_t ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000};
    nanosleep(&ts, NULL);
}
int DG_GetKey(int* pressed, unsigned char* key) {
    (void)pressed; (void)key;
    return 0;
}
void DG_SetWindowTitle(const char* title) { (void)title; }
void DG_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}
void* ps_malloc(size_t size) { return malloc(size); }
// The headless -nosound path does not use these modules, but the engine's
// sound file still references their symbols at link time.
sound_module_t* DG_sound_module = NULL;
music_module_t* DG_music_module = NULL;
int use_libsamplerate = 0;
float libsamplerate_scale = 0;

void DG_DrawFrame(void) {
    if (gamestate != GS_LEVEL || menuactive) return;
    if (capture_wipe ? !D_WipeInProgress() : !demoplayback) return;
    if (++demo_frames < requested_frame) return;
    FILE* output = fopen(output_path, "wb");
    if (!output) exit(3);
    fprintf(output, "P6\n320 240\n255\n");
    for (int y = 0; y < 240; ++y) {
        for (int x = 0; x < 320; ++x) {
            const uint32_t pixel = DG_ScreenBuffer[y * 320 + x];
            const unsigned char rgb[3] = {
                (pixel >> 16) & 255, (pixel >> 8) & 255, pixel & 255
            };
            fwrite(rgb, 1, 3, output);
        }
    }
    fclose(output);
    fprintf(stderr, "Captured menu-free DEMO1 %s frame %d\n",
            capture_wipe ? "wipe" : "gameplay", demo_frames);
    exit(0);
}
int main(int argc, char** argv) {
    if (argc < 3 || argc > 5) {
        fprintf(stderr, "usage: capture IWAD.WAD output.ppm [frame] [wipe]\n");
        return 2;
    }
    output_path = argv[2];
    if (argc >= 4) requested_frame = atoi(argv[3]);
    if (argc == 5) capture_wipe = strcmp(argv[4], "wipe") == 0;
    if (requested_frame < 1) return 2;
    char* doom_args[] = {
        "doomgeneric", "-iwad", argv[1], "-playdemo", "demo1",
        "-nosound", "-nomusic", NULL
    };
    D_AllocBuffers();
    doomgeneric_Create(7, doom_args);
    const uint32_t deadline = now_ms() + 30000;
    while (now_ms() < deadline) doomgeneric_Tick();
    fprintf(stderr, "Timed out after %d gameplay frames\n", demo_frames);
    return 4;
}
