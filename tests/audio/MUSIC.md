# Stock WAD music (source-only feature)

Branch feature/wad-music, based on volume/input feature commit 6f20802.

## Implementation and scope

- Real integer DOSBox DBOPL FM core, OPL2 mode, nine physical 2-operator
  channels, 11025Hz mono. Uses WAD GENMIDI at runtime, not a bundled bank.
  See src/opl/PROVENANCE.md for source, license and dependency selection.
- GENMIDI 175 x 36-byte records: two packed 16-byte voices, operator
  tremolo/multiplier, attack/decay, sustain/release, waveform, KSL/TL,
  feedback/connection, fixed-note and signed base-note offsets. Doublevoice
  flag allocates two OPL channels; secondary fine tuning is applied. More
  than nine operators-pairs steals oldest voice, independently of SFX.
- MUS header/start/length/instrument-table bounds, 140Hz rational sample
  clock, 1–4-byte variable deltas, channel velocity reuse, note on/off,
  programs, +/-2 semitone bend, channel volume/expression, sustain,
  all-notes/sounds-off and reset controllers. Channel 15 percussion notes
  35–81 select GENMIDI records 128–174 as melodic OPL patches (Doom style,
  not OPL hardware rhythm mode). Loop, pause/resume, stop/is-playing.
- Bank select, modulation, pan, reverb, chorus, soft pedal and mono/poly
  hints are parsed safely but have no audible action; physical output is
  mono. This is not a bit-exact DMX driver: logarithmic velocity/channel
  attenuation uses a bounded approximation, frequency calculation uses
  1/256-semitone precomputed octave table with hardware-range clipping,
  voice stealing is oldest-first, and fine-tuning convention is approximate.
  Pause freezes both sequencing and FM envelopes and outputs music silence.
  Stop/end discards tails; noteoff while playing retains FM release.
- Only MUS is supported. MIDI, MP3/OGG, arbitrary PWAD music replacements
  and external soundfont formats are unsupported and rejected at registration.
  No claim of universal WAD soundtrack compatibility.

## Lifetime and one shared output

DG_music_module callbacks now live in i_i2ssound.cpp, beside Mixer and
MusOPL, under the existing mixerMutex. Default stub is removed. Engine
selects the module in I_InitSound; actual I_InitMusic initializes it after
WAD startup. Init failure clears module selection. Music shutdown precedes
SFX shutdown; it stops/unregisters before shared driver teardown and is
idempotent. -nosfx music can initialize the same output by itself. Startup
piezo / No sound selection remains honored: music only initializes when
DG_sound_module selects I2S. Standalone music shutdown preserves SFX-owned
output; only music-only output is torn down by the music callback.
I_ShutdownMusic now delegates instead of being an empty engine stub.

GENMIDI is copied on engine thread then WAD cache reference is released.
Register validates header then copies only bounded score bytes on engine
thread (at most 65535 bytes). One registration at a time deliberately
matches Doom's existing stop/unregister/register sequence. No stale-handle
API beyond that lifecycle is promised. Unregister waits mixerMutex, detaches
song, then frees storage; output cannot retain a borrowed WAD/song pointer.
Output accesses pinned SFX bytes and copied bank/song only: no WAD/zone,
NVS, malloc/free/new allocation. Placement reconstruction of chip happens
only during engine-thread init, not output.

One unchanged soundTask / native I2S0 driver / DMA setup and writer. Music
is added to int32 effect sum BEFORE SDK live master gain and one final
saturation. Doom music gain (0–127) is independent of existing SFX voice
gains. Existing partial-write, live master mute and hard-error behavior
are retained. No SDK source, input/overlay, or effect voice logic changed.
Pending output may contain up to a 128-sample pre-rendered block when a
callback takes effect, same bounded buffering semantics as effects.

## Bounds and host validation

Normal event group bound: 1024 events/sample including zero-delay loops;
malformed/truncated events or overlong delta fail closed. Each event scans
at most 9 FM voices; controllers scan at most 9; note may allocate two.
Each rendered sample generates 9 OPL2 channels and adds one music gain.
Initialization tables/math happen only on engine thread. MUS score copy
is <=65535 bytes; GENMIDI copy 6300 bytes; frequency table 6144 bytes;
chip/voice/channel state is fixed. sizeof(MusOPL) on x86-64 is 18320 bytes
(including those copies); DBOPL global tables are additional. No device
RAM/flash, stack watermark or realtime claim follows from host sizes/times.

Run sh tests/audio/run.sh: actual production I2S backend with mocks plus
DBOPL, ASan+UBSan (halt-on-error, leak checks), normal optimized tests,
and C++11 core compilation. Synthetic fixtures are authored test-only
MUS and operator records, not copied WAD assets. Tests cover nonzero FM,
GENMIDI audible changes, exact long/short deltas, loop/pause, music gains,
master mute, velocity reuse, pressure/doublevoice/percussion/bend, malformed
and hostile score/bank fuzz, copied registration cleanup, single install,
and effects+music arithmetic without suppression. Existing effects tests
remain in the same executable. python3 tests/volume/run.py covers protected
regular overlay and actual shortcut/input/render behavior unchanged.

## Device release gates (NOT performed)

No firmware build, PlatformIO, dependency download/install, package, flash,
new worktree or .pio/cache creation. platformio.ini points lib_dir at ../sdk/lib
and includes ./lib; no dependency changes. An authorized fresh firmware
build must prove recursive src/opl compilation, intended local SDK path,
ESP32 C++ toolchain/link compatibility, RAM/flash delta versus feature
baseline, and deployment artifact identity. Device then needs stock WAD
title/menu/level music listening, SFX+music busy-scene timing, no starvation,
long looping soak, suspend/resume/level change/unregister/shutdown, master
live mute/partial backpressure, heap/stack watermarks, and overlay inputs.
Do not infer ESP32 realtime feasibility or listening fidelity from host FM.
