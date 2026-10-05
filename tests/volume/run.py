#!/usr/bin/env python3
"""Host-test actual Doom input and render loop with the real SDK font."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
SDK = Path(os.environ.get("LILKA_SDK", ROOT.parent / "sdk"))
FONT = Path(os.environ.get("U8G2_CLIB", ROOT.parent / "lilka-sdk/lib/lilka/.pio/libdeps/v2/U8g2/src/clib"))
def function(text, signature):
    start = text.index(signature + " {")
    brace = text.index("{", start)
    end, depth = brace + 1, 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]
main = (ROOT / "src/main.cpp").read_text()
menu = (ROOT / "lib/doomgeneric/src/m_menu.c").read_text()
assert "key == key_menu_activate || key == KEY_ENTER" in menu
assert "if (key == KEY_FIRE)" in menu and "key = key_menu_forward;" in menu
assert "else if (key == KEY_USE)" in menu and "key = key_menu_back;" in menu
with tempfile.TemporaryDirectory(prefix="doom-volume-") as directory:
    tmp = Path(directory)
    (tmp / "production.inc").write_text(function(main, "void buttonHandler(lilka::Button button, bool pressed)") + "\n" + function(main, "void drawTask(void* arg)") + "\n")
    objects = []
    for name in ("u8g2_font.c", "u8g2_fonts.c", "u8g2_hvline.c", "u8g2_intersection.c"):
        obj = tmp / (name + ".o")
        subprocess.run(["gcc", "-O1", "-ffunction-sections", "-fdata-sections", "-I" + str(FONT), "-c", str(FONT / name), "-o", str(obj)], check=True)
        objects.append(str(obj))
    for flags in ([], ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]):
        subprocess.run(["g++", "-std=c++11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", *flags, "-I" + str(tmp), "-I" + str(SDK / "lib/lilka/src"), "-I" + str(FONT.parent), "-I" + str(ROOT / "lib/doomgeneric/src"), str(ROOT / "tests/volume/regression.cpp"), *objects, "-Wl,--gc-sections", "-o", str(tmp / "test")], check=True)
        subprocess.run([str(tmp / "test")], check=True)
