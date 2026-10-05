# Doom SFX source-only checks

Run `sh tests/audio/run.sh`. Requires existing g++; no firmware build, PlatformIO, download, or installation. Compiles the actual production mixer and backend with engine declarations and mocked WAD/FreeRTOS/I2S services under ASan + UBSan; temporary outputs are removed. Also checks pure core C++11 compatibility.

## Design and contracts

The in-repo Doom engine (`s_sound.c`, `i_sound.h`) already selects/steals channels and supplies per-effect volume 0..127. Preserve those semantics; no additional priority scheduler or music DSP. Available alternative in-repo backends are buzzer/no-sound, not reusable PCM mixers. SDL mixing is feature-gated and requires unavailable SDL_mixer on the MCU. Sixteen fixed slots cover default eight engine channels; indices outside 0..15 fail with -1. Unique nonwrapping handles prevent stale stop/update from affecting replacements (exhaustion at INT_MAX starts fails). Physical output is mono; stereo separation intentionally does not attenuate or pan.

DMX format 3: eight-byte header, nonzero little-endian rate, declared sample length within lump bounds and >48, sixteen guard samples skipped at each end of the payload (data+24, not data+16). Guard byte values are not constrained; extra bytes beyond declared payload are ignored. Integer nearest-neighbor phase resamples 1..65535 Hz to 11025 Hz; no interpolation or antialiasing claim. Signed 32-bit additive accumulation, per-effect gain /127, live SDK master /100 once before int16 saturation. Muted voices still advance.

W_CacheLumpNum(PU_SOUND) pins data instead of purgeable PU_CACHE. The existing WAD API supports mapped data and W_ReleaseLumpNum; reaping releases each lump only after the final voice reference, including completed voices. Zone/cache operations remain on the single Doom engine thread. Audio only renders pinned bytes under the mixer mutex. Engine callbacks/lifecycle must remain serialized on that thread; arbitrary simultaneous engine callbacks and shutdown are not supported by the engine itself. Shutdown atomically requests stop, waits for task acknowledgement, then uninstalls only its owned driver and releases pins/mutexes.

I2S blocking writes are outside mixer/engine locks, bounded by 20ms plus one RTOS tick. Partial writes advance exact aligned bytes; zero-progress timeout yields one tick then retries the pending block. Hard errors or malformed counts stop output safely until shutdown/reinit. Normal driver backpressure paces continuously supplied silence. Already accepted PCM/DMA cannot be retroactively stopped/muted. Pending partial writes re-read live master volume.

SDK Audio::begin starts a delayed welcome task which can compete for I2S0. Driver install failure is reported, never overridden/uninstalled; source-only code cannot guarantee welcome completion. Normal picker/startup probably outlasts hello, but this requires device validation. No SDK/main/config change or hidden welcome suppression was made.

## Bounded costs

No 128KiB sample ring. Mixer: 16 fixed voices, host sizeof 648 bytes (40/voice); MCU size must be measured at authorized build. Output task: 3072-byte requested stack, with 512-byte wide block + 256-byte PCM block; two DMA blocks of 128 samples (driver overhead unmeasured). WAD cache memory equals unique retained lump sizes, at most 16 references, and is released on engine polling/stopping/shutdown. Hot path has 128*16 maximum voice visits/block, about 86.1 blocks/sec at 11025Hz, one render mutex acquisition/block; no allocation, NVS or WAD I/O on output task. Reaping is bounded 16*16 scans on engine callbacks. No lock held during blocking I2S; cache loads occur before acquiring mixer lock.

## Coverage and remaining gates

Host tests: DMX truncations/header/rate/length/guards; positive/negative additive overlap and saturation; resampling at 1/5512/11025/22050/44100/65535Hz; gain/mute; voice capacity/restart/stale handles/update/stop/isplaying; shared-lump pinning; invalid/missing lumps; partial/zero timeout/error/malformed write handling; continuous inactive silence; allocation/install/DMA/task init failures and cleanup; blocked output while engine callbacks proceed; 2000 start/update/stop interleavings with rendering; shutdown while active. The mocks assert no write under mixer lock and engine-only lump retention. ASan/UBSan are not ThreadSanitizer or a hardware proof.

Formatting CI requires clang-format-17 (`make clang-format`), static analysis `make cppcheck`. Both were attempted but binaries were unavailable, including supplied /tmp formatter paths; no packages installed. Parent must run those gates with an existing formatter/analyzer. `git diff --check` and sanitizer host compilation pass. Firmware compilation/linkage, SDK dependency selection, I2S/DMA pin routing and startup race, mono ordering, RTOS stack/heap watermark, real overlap listening, master latency/clipping and long-run stress remain explicit authorized build/device gates.
