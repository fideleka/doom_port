# DBOPL provenance and local changes

Vendored from local ScummVM-Lilka repository
/home/anton/projects/scummvm-lilka at commit
c2a81638197fcc0db7e543b6399b986a9256a8db:
- audio/softsynth/opl/dbopl.cpp
- audio/softsynth/opl/dbopl.h

Original copyright notices remain intact: DOSBox Team 2002–2011,
GPL version 2 or later. Doomgeneric is GPL version 2 or later, so this
component can be combined under GPL-2.0-or-later. License text is in
lib/doomgeneric/LICENSE (repository existing GPL text).

Lightweight local existing-solutions check: examined these two files and
ScummVM audio/fmopl.cpp,h. DBOPL directly exposes register writes and mono
OPL2 generation; fmopl is a higher-level ScummVM mixer/chip dependency
wrapper (GPL-3.0-or-later), deliberately NOT vendored. No downloads,
packages, ScummVM runtime/mixer, game assets, or network lookup used.
DBOPL's original comment identifies its last DOSBox sync as SVN r3752;
this is provenance, NOT a claim this snapshot is current upstream.

Local adapters:
- dbopl.h includes standalone types.h instead of common/scummsys.h;
  fixed-width type aliases, C library/math includes only.
- dbopl.cpp uses offsetof for standard-layout Chip/Channel/Operator
  instead of null-pointer-derived member offsets (UBSan violation).
- Attack-rate initialization multiply/shift widened to int64_t to avoid
  observed signed overflow at 11025Hz. Negative percussion PCM doubling
  uses multiplication instead of undefined signed left shift.
- mus_opl.h is newly authored GPL-2.0-or-later sequencing and GENMIDI
  adapter, not copied game data or an improvised waveform synthesizer.

Keep upstream notices and this change list with redistribution. Future
upstream updates should be diff-reviewed against the above local fixes.
