#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 2 ]]; then
  echo "Usage: $0 IWAD.WAD output-prefix [ammo health armor]" >&2
  exit 2
fi
wad_path=$1
output_prefix=$2
shift 2
preview_exe=$(mktemp /tmp/doom-hud-preview.XXXXXX)
trap 'rm -f "$preview_exe"' EXIT
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Isrc tools/hud_preview.cpp -o "$preview_exe"
"$preview_exe" "$wad_path" "${output_prefix}.ppm" "$@"
convert "${output_prefix}.ppm" "${output_prefix}.png"
convert "${output_prefix}.png" -filter point -resize 1120x960 "${output_prefix}-4x.png"
convert "${output_prefix}.png" -crop 280x48+0+184 +repage -filter point -resize 1120x192 "${output_prefix}-hud-4x.png"
