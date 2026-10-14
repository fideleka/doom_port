#!/usr/bin/env python3
"""Compile/exercise extracted production startup; no firmware toolchain needed."""
from pathlib import Path
import subprocess
import tempfile
import re
import argparse
ROOT = Path(__file__).resolve().parents[2]
def function(text, signature):
    start = re.search(re.escape(signature) + r"\s*\{", text).start()
    brace = text.index("{", start)
    end, depth = brace + 1, 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]
parser = argparse.ArgumentParser()
parser.add_argument("--source", type=Path, default=ROOT / "src/main.cpp")
parser.add_argument("--late-reservation", action="store_true")
args = parser.parse_args()
main = args.source.read_text()
setup = function(main, "void setup()")
assert "initializeDoomRuntime(argc, argv);" in setup
assert setup.index("reserveDoomTasks()") < setup.index("lilka::begin()")
for signature in ("void gameTask(void* arg)", "void drawTask(void* arg)"):
    assert function(main, signature).split("{", 1)[1].lstrip().startswith("waitForEngineStart();")
assert "first frame presented" not in main
assert "logStartupHeap" not in main
startup = main[main.index("constexpr uint32_t gameStackBytes"):main.index("void setup()")]
alloc = (ROOT / "lib/doomgeneric/src/d_alloc.c").read_text()
engine = (ROOT / "lib/doomgeneric/src/doomgeneric.c").read_text()
production = function(alloc, "int D_TryAllocBuffers(void)") + "\n" + function(engine, "void doomgeneric_Create(int argc, char **argv)") + "\n" + function(main, 'extern "C" void DG_DrawFrame()') + "\n" + startup
with tempfile.TemporaryDirectory(prefix="doom-startup-") as directory:
    tmp = Path(directory)
    (tmp / "production.inc").write_text(production)
    for flags in ([], ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]):
        subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-g", "-O1", *flags, *(["-DLATE_RESERVATION"] if args.late_reservation else []), "-I" + str(tmp), str(ROOT / "tests/startup/regression.cpp"), "-o", str(tmp / "test")], check=True)
        subprocess.run([str(tmp / "test")], check=True)
