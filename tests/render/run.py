#!/usr/bin/env python3
"""Synthetic pixels through actual engine renderer; no firmware/WAD needed."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "lib/doomgeneric/src"
with tempfile.TemporaryDirectory(prefix="doom-render-") as directory:
    tmp = Path(directory)
    (tmp / "esp_heap_caps.h").write_text("#pragma once\n")
    for flags in ([], ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]):
        objects = []
        for name in ("r_sky.c", "r_draw.c", "r_plane.c", "r_main.c", "m_fixed.c", "tables.c", "m_random.c"):
            obj = tmp / (name + ".o")
            subprocess.run(["gcc", "-std=gnu11", "-O1", "-g", "-ffunction-sections", "-fdata-sections", *flags, "-I" + str(tmp), "-I" + str(SOURCE), "-c", str(SOURCE / name), "-o", str(obj)], check=True)
            objects.append(str(obj))
        subprocess.run(["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", *flags, "-I" + str(SOURCE), str(ROOT / "tests/render/sky.c"), *objects, "-Wl,--gc-sections", "-lm", "-o", str(tmp / "test")], check=True)
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1", UBSAN_OPTIONS="halt_on_error=1")
        subprocess.run([str(tmp / "test")], check=True, env=env)
