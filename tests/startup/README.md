# Startup resource regression

Run `python3 tests/startup/run.py`.
The runner extracts production startup, buffer-allocation and frame-swap functions,
compiles C++11 with warnings as errors, then repeats with ASan/UBSan.
It injects failures into all 11 allocation sites: two mutexes, event group,
two PSRAM framebuffers, renderer/game tasks, and four engine buffer groups.
Every failure must clean up, show the persistent failure UI, and never launch
Doom or notify either task. Success must preserve the engine's preallocated
framebuffer, swap/publish the startup frame, and release both notification gates.

## Resource change / device validation

* Game task remains 32768 bytes, draw task becomes 16384 bytes: 16384 fewer
  task-stack bytes. Priorities and cores remain unchanged.
* Renderer maps plus scanline total 2240 bytes. The overlay has one local
  u8g2 decoder (248 bytes in the host ABI); glyph decoding is iterative and
  callbacks touch only RAM. The v2 pixel path is TFT writePixels -> HWSPI
  writePixels/WRITEBUF -> SPIClass::writeBytes -> spiWriteNL: scalar locals,
  an object buffer, and peripheral registers (no scanline-sized nested stack
  array). This is source justification, not an ESP32
  stack-watermark measurement.
* Both 320x240 RGB565 frames explicitly use PSRAM (307200 bytes total),
  checked before engine callbacks. Previously their generic malloc location
  depended on the ESP-IDF allocation configuration.
* Tasks reserve their stacks BEFORE engine/audio initialization, but remain
  blocked until BOTH task creations and engine initialization succeed. Thus
  engine progress can never silently run without a successfully created renderer.
* FM synthesis, music lifetime, SFX mixing, mappings and WAD selection are unchanged.

Capture serial startup lines: internal free/largest block and PSRAM free/largest
block before tasks, after reservation, after engine init, each task result/stack,
and the single first-frame-presented line. Failure logs include heap state before
cleanup; the screen holds the reason until a manual reboot.

Device boot with I2S music + effects, menu/overlay/wipe exercise, and renderer
stack watermark/soak are still required. Source confirms the former unchecked
renderer task creation could leave a ticking, audible game without a display;
actual device allocation failure is a hypothesis until startup logs confirm it.
No firmware build, installation, download, packaging or flashing is part of this test.
