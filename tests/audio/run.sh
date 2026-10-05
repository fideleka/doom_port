#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
build=$(mktemp -d /tmp/doom-audio.XXXXXX)
trap 'rm -rf "$build"' EXIT HUP INT TERM

g++ -std=c++17 -Wall -Wextra -Werror -g -O1     -fsanitize=address,undefined -fno-omit-frame-pointer -pthread     -Itests/audio/mocks -Ilib/doomgeneric/src tests/audio/audio_test.cpp -o "$build/audio-test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/audio-test"
printf '#include "src/sfx_mixer.h"
' |     g++ -std=c++11 -Wall -Wextra -Werror -I. -x c++ -c - -o "$build/core-cxx11.o"
