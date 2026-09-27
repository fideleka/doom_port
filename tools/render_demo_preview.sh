#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 2 ]]; then
  echo "usage: $0 IWAD.WAD output-prefix [frame] [wipe]" >&2
  exit 2
fi
wad_path=$(realpath "$1")
output_prefix=$(realpath -m "$2")
demo_frame=${3:-150}
capture_mode=${4:-normal}
preview_tmp=$(mktemp -d /tmp/doom-demo-capture.XXXXXX)
trap 'rm -rf "$preview_tmp"' EXIT
printf '#pragma once\n' > "$preview_tmp/esp_heap_caps.h"
python3 - "$preview_tmp" <<'PY'
from pathlib import Path
import subprocess
import sys
source = Path('lib/doomgeneric/src')
output = Path(sys.argv[1])
line = next(line for line in (source / 'Makefile').read_text().splitlines()
            if line.startswith('SRC_DOOM ='))
names = [name for name in line.split('=', 1)[1].split()
         if name != 'doomgeneric_xlib.o'] + ['d_alloc.o']
objects = []
for name in names:
    obj = output / name
    subprocess.run(['gcc', '-std=gnu11', '-O2', '-w', '-D_DEFAULT_SOURCE',
                    '-include', 'stdint.h', f'-I{source}', f'-I{output}',
                    '-c', str(source / name.replace('.o', '.c')), '-o', str(obj)],
                   check=True)
    objects.append(str(obj))
subprocess.run(['gcc', '-std=gnu11', '-O2', '-w', '-D_DEFAULT_SOURCE',
                '-include', 'stdint.h', f'-I{source}', f'-I{output}',
                '-c', 'tools/headless_demo_capture.c', '-o', str(output / 'backend.o')],
               check=True)
subprocess.run(['gcc', *objects, str(output / 'backend.o'), '-lm',
                '-o', str(output / 'capture')], check=True)
PY
(cd "$preview_tmp" && timeout 40s ./capture "$wad_path" "${output_prefix}-source.ppm" "$demo_frame" "$capture_mode") > "${output_prefix}-capture.log" 2>&1
python3 tools/render_demo_preview.py "${output_prefix}-source.ppm" "$wad_path" "$output_prefix" "$capture_mode"
if command -v convert >/dev/null 2>&1; then
  convert "${output_prefix}-stage.ppm" "${output_prefix}-stage.png"
  convert "${output_prefix}-candidate.ppm" "${output_prefix}-candidate.png"
  convert "${output_prefix}-stage.png" "${output_prefix}-candidate.png" +append "${output_prefix}-compare.png"
fi
