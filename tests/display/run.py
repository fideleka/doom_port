#!/usr/bin/env python3
"""Actual Doom Display helper, scripted host keys; not a firmware build."""
from pathlib import Path
import subprocess
import tempfile
import re

root = Path(__file__).resolve().parents[2]
main = (root / "src/main.cpp").read_text()
assert re.findall(r'soundMenu\.addItem\("([^"\n]*)"\)', main) == ["I2S DAC", "П'єзо-динамік", "Без звуку"]
assert "doomDisplay::showSettings()" not in main
assert main.index("lilka::begin();") < main.index("lilka::displaySettings.begin();") < main.index("pickWad(firmwareDir")
assert main.index("displaySettings.serviceIdle(false);") < main.index("startBootConsole(arg3);")
assert "serviceStartupIdle()" in (root / "src/wad_picker.cpp").read_text()
mock = r"""
#pragma once
#include <cassert>
#include <string>
#include <vector>
#include <cstdint>
struct String:std::string {using std::string::string;String(const std::string&s):std::string(s){}String(unsigned n):std::string(std::to_string(n)){}String(int n):std::string(std::to_string(n)){} };
inline void vTaskDelay(int){}
#define pdMS_TO_TICKS(x) (x)
namespace lilka {
enum class Button{A,B};namespace colors{const int White=1;}
struct Key{bool justPressed=false;};struct State{Key left,right,a,d;bool selectHeld=false;};
struct Input{int row;State state;};std::vector<Input> script;unsigned tick=0,renders=0;bool sleepOnce=false;
struct Canvas{};
struct Brightness{bool enabled=true;int level=55,steps=0;bool isEnabled(){return enabled;}int getBrightness(){return level;}void stepBrightnessShortcut(int n){if(enabled){level+=n*5;steps++;}}}brightness;
struct Settings{uint32_t off=0,dim=0;bool sleeping=false;unsigned calls=0;bool isAvailable(){return true;}uint32_t getTimeoutSeconds(){return off;}uint32_t getDimTimeoutSeconds(){return dim;}void setTimeoutSeconds(uint32_t n){off=n;}void setDimTimeoutSeconds(uint32_t n){dim=n;}void serviceIdle(bool eligible){assert(eligible);calls++;sleeping=sleepOnce;sleepOnce=false;}bool isSleeping(){return sleeping;}}displaySettings;
struct Controller{State peekState(){return script.at(tick).state;}}controller;
struct Menu{bool horizontal=true,aRemoved=false,bAdded=false;unsigned items=0;Menu(const char*){}void addItem(const char*){items++;}void addActivationButton(Button b){assert(b==Button::B);bAdded=true;}void removeActivationButton(Button b){assert(b==Button::A);aRemoved=true;}void setHorizontalNavigationEnabled(bool v){horizontal=v;}bool isFinished(){return tick>=script.size();}int getCursor(){return script.at(tick).row;}void setItem(int,const char*,void*,int,String){}void update(){assert(items==3&&!horizontal&&aRemoved&&bAdded);tick++;}void draw(Canvas*){}};
struct Display{void drawCanvas(Canvas*){renders++;}}display;
}
"""
checks = r"""
#include "display_settings.h"
int main(){using namespace lilka;
assert(doomDisplay::timeoutText(0)=="Never");assert(doomDisplay::timeoutText(30)=="30 s");assert(doomDisplay::timeoutText(120)=="2 min");
for(bool dim:{false,true}){for(unsigned expected:{30,60,120,300,600,600}){doomDisplay::changeTimeout(1,dim);assert((dim?displaySettings.dim:displaySettings.off)==expected);}for(unsigned expected:{300,120,60,30,0,0}){doomDisplay::changeTimeout(-1,dim);assert((dim?displaySettings.dim:displaySettings.off)==expected);}}
State right;right.right.justPressed=true;State left;left.left.justPressed=true;State select=right;select.selectHeld=true;State both=right;both.left.justPressed=true;
script={{0,right},{1,right},{2,right},{0,select},{0,both},{0,left}};sleepOnce=true;doomDisplay::showSettings();assert(brightness.level==55&&brightness.steps==2);assert(displaySettings.off==30&&displaySettings.dim==30);assert(renders==script.size());assert(displaySettings.calls==script.size()+1);
brightness.enabled=false;tick=0;script={{0,right},{2,right}};doomDisplay::showSettings();assert(brightness.steps==2&&displaySettings.dim==30);
puts("Doom Display presets, shared APIs, three-row controls, modifier exclusion and sleep guard PASS");}
"""
with tempfile.TemporaryDirectory(prefix="doom-display-") as d:
    p = Path(d)
    (p / "lilka.h").write_text(mock)
    (p / "test.cpp").write_text(checks)
    for flags in ([], ["-fsanitize=address,undefined", "-fno-pie", "-no-pie"]):
        subprocess.run(
            [
                "g++",
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                *flags,
                "-I" + str(p),
                "-I" + str(root / "src"),
                str(p / "test.cpp"),
                "-o",
                str(p / "test"),
            ],
            check=True,
        )
        subprocess.run([str(p / "test")], check=True)
