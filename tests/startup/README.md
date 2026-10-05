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

Temporary heap/task-success/first-frame diagnostics were removed after the owner
confirmed all tested WADs work on the device. Successful startup is quiet.
Allocation checks remain; failures still log their reason and hold it on screen
until a manual reboot.

The owner confirmed working device playback and display for all WADs they tested.
Renderer stack watermark/extended soak remain unmeasured. Source confirms the
former unchecked renderer creation could leave an audible game without a display;
the precise original allocation failure was not captured.
No firmware build, installation, download, packaging or flashing is part of this test.
