# Modified-hardware SDK brightness

This feature branch starts from this application's staging branch and uses the
SDK `features/stage` (or downstream `features/stage-lilplayer`) sibling checkout (`../sdk`).

Use environment **`v2-modified-backlight`** only after isolating the amplifier
module SD connection from GPIO46 and adding independent bias. This inherits the
normal application and enables `-DLILKA_INDEPENDENT_BACKLIGHT=1`; default builds
remain stock-safe.

Hold Select first, then Left/Right to change brightness. The SDK owns PWM,
feedback, delayed NVS saves and sleep/wake restoration. No test-screen startup
is included. Selected brightness is clamped to **5..100%**; legacy saved zero loads as 5%.
Full LCD-off is separate from selected brightness.

Read `../sdk/docs/INDEPENDENT_BACKLIGHT.md` for APIs, peripheral reservations,
verification details and remaining hardware checks. No firmware build/flash
was performed for this integration.

Reviewed general SDK stage: `310ac2cdda5ea1708c79da69a56f370241047b74`
(downstream radio stage `ba7a1692259e277b82e47df9c814617dc41bfff1`).

## Display controls

Choose **Display** in the startup sound-device menu, after choosing the WAD.
Up/Down selects Brightness / Auto-off / Idle dim; Left/Right (or D/A)
adjusts; B returns to the sound-device menu. Select-first Left/Right continues
to work globally, including in the game, with the SDK sun/percentage overlay.
The Display menu does not map Select chords to a second adjustment.

Both timers default **Never** and offer 30 s / 1 / 2 / 5 / 10 min. These are the
same shared `backlight/level`, `timeoutSeconds`, `dimSeconds` keys used by Keira
and Lilplayer; existing saved choices are retained. Temporary dim uses 5%
without changing selected/saved brightness. Any key gesture wakes the screen
and is consumed until all buttons release. Idle timers run only in WAD,
sound-device and Display startup menus; the selected brightness is restored
before startup/engine work. Gameplay (including its own menus/pause) is an
active app and never automatically dims or switches off.

Validation: actual helper controls/presets/sleep guard normal + ASan/UBSan;
existing startup, audio, volume/overlay and render host regressions; native SDK
menu layout/font inspected at 240x280 and 280x240. These are host/source checks,
not device certification. No firmware build, flash or dependency installation.

Doom scanline presentation now explicitly chooses the newest visible SDK volume
or brightness snapshot, matching SDK Canvas presentation. Brightness sun/percent
and bar therefore refresh on retained/paused frames and expire normally. Host
regressions cover brightness-only, volume-only, both recency orders and equal
timestamps, regular/UI/wipe frames and both orientations; actual host LCD
captures were visually inspected. Device verification remains outstanding.
