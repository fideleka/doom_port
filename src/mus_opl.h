// GPL-2.0-or-later. Runtime MUS / GENMIDI adapter; no game assets embedded.
#pragma once
#include "sfx_mixer.h"
#include "opl/dbopl.h"
#include <cmath>
#include <cstring>
#include <new>
namespace doom_audio {
struct Song {
    const uint8_t* bytes = nullptr;
    size_t length = 0;
    static bool parse(const uint8_t* p, size_t n, Song& out) {
        if (!p || n < 16 || memcmp(p, "MUS\x1a", 4)) return false;
        size_t start = p[6] | (size_t(p[7]) << 8);
        size_t len = p[4] | (size_t(p[5]) << 8);
        size_t instruments = p[12] | (size_t(p[13]) << 8);
        if (start < 16 + instruments * 2 || start > n || !len || len > n-start) return false;
        out.bytes = p+start; out.length = len; return true;
    }
};
class MusOPL {
    struct Channel { int program=0, volume=127, expression=127, velocity=127, bend=128; bool sustain=false; } channels[16];
    struct Voice { int channel=-1, note=0, velocity=0, instrument=0, part=0; uint32_t age=0; bool held=false; } voices[9];
    using Chip = OPL::DOSBox::DBOPL::Chip;
    Chip chip;
    uint8_t bank[175*36] = {};
    uint16_t frequencies[12*256] = {}; // precomputed on engine thread, no floating math in output
    const Song* song=nullptr;
    size_t cursor=0;
    uint32_t delay=0, phase=0, serial=0;
    bool playing=false, paused=false, loop=false, ready=false;
    int volume=127;
    static int op(int v) { return (v/3)*8 + v%3; }
    void reg(int r,int v) { chip.WriteReg(r, uint8_t(v)); }
    void off(int v) { if (ready) reg(0xb0+v, keyFrequency[v] >> 8); voices[v].channel=-1; }
    uint16_t keyFrequency[9] = {};
    void clear() { for(int i=0;i<9;i++) off(i); }
    bool byte(uint8_t& v) { if(!song || cursor>=song->length) return false; v=song->bytes[cursor++]; return true; }
    void pitch(int v) {
        auto& a=voices[v]; const uint8_t* ins=bank+a.instrument*36;
        const uint8_t* part=ins+4+a.part*16;
        int note=(ins[0]&1) ? ins[3] : a.note;
        int offset=int(int16_t(uint16_t(part[14]) | (uint16_t(part[15])<<8)));
        int index=(note+offset)*256+(channels[a.channel].bend-128)*4;
        if(a.part) index+=int(ins[2])-128;
        index=index<0?0:(index>32767?32767:index);
        int block=index/(12*256);
        int number=frequencies[index%(12*256)];
        if(block>7) { number <<= block-7; block=7; }
        uint16_t f=uint16_t(clamp(number,1023)|(block<<10)); keyFrequency[v]=f; reg(0xa0+v,f&255); reg(0xb0+v, (f>>8)|32);
    }
    void levels(int v) {
        auto& a=voices[v]; const uint8_t* p=bank+a.instrument*36+4+a.part*16;
        auto& c=channels[a.channel];
        int gain=a.velocity*c.volume*c.expression/(127*127);
        // Logarithmic attenuation in OPL's 0.75dB steps, table-free bounded integer approximation.
        int attenuation=63; if(gain>0) { attenuation=0; while(gain<64){gain*=2;attenuation+=8;} attenuation+=(127-gain)/8; }
        for(int j=0;j<2;j++) { const uint8_t* o=p+j*7; int tl=o[5]&63;
            if(j || (p[6]&1)) tl=clamp(tl+attenuation,63);
            reg(0x40+op(v)+j*3,(o[4]&192)|tl);
        }
    }
    int allocate() {
        for(int i=0;i<9;i++) if(voices[i].channel<0) return i;
        int best=0; for(int i=1;i<9;i++) if(voices[i].age<voices[best].age) best=i;
        off(best); return best;
    }
    void noteOn(int ch,int note,int vel) {
        int ins=ch==15 ? 128+note-35 : channels[ch].program;
        if(ins<0 || ins>=175) return;
        if(!vel) { noteOff(ch,note); return; }
        int parts=(bank[ins*36]&4)?2:1;
        for(int part=0;part<parts;part++) {
            int v=allocate(); auto& voice=voices[v];
            voice.channel=ch; voice.note=note; voice.velocity=vel;
            voice.instrument=ins; voice.part=part; voice.age=++serial; voice.held=false;
            const uint8_t* p=bank+ins*36+4+part*16;
            reg(0xb0+v,0);
            for(int j=0;j<2;j++) { const uint8_t* o=p+j*7; int addr=op(v)+j*3;
                reg(0x20+addr,o[0]); reg(0x60+addr,o[1]); reg(0x80+addr,o[2]); reg(0xe0+addr,o[3]&3);
            }
            reg(0xc0+v,p[6]&15); levels(v); pitch(v);
        }
    }
    void noteOff(int ch,int note) { for(int i=0;i<9;i++) if(voices[i].channel==ch && voices[i].note==note) {
        if(channels[ch].sustain) voices[i].held=true; else off(i);
    } }
    void control(int ch,int c,int val) {
        auto& a=channels[ch];
        if(c==0) a.program=val;
        else if(c==3) a.volume=val;
        else if(c==5) a.expression=val;
        else if(c==8) { a.sustain=val>=64; if(!a.sustain) for(int i=0;i<9;i++) if(voices[i].channel==ch && voices[i].held) off(i); }
        else if(c==10 || c==11) { for(int i=0;i<9;i++) if(voices[i].channel==ch) off(i); }
        else if(c==14) { int program=a.program; a=Channel{}; a.program=program; for(int i=0;i<9;i++) if(voices[i].channel==ch && voices[i].held) off(i); }
        for(int i=0;i<9;i++) if(voices[i].channel==ch) levels(i);
    }
    void events() {
        // Hard per-sample bound, including zero-time looped scores. Malformed data fails closed.
        for(int budget=0;budget<1024;budget++) {
            uint8_t e,a,b; if(!byte(e)) { stop(); return; }
            int ch=e&15, type=(e>>4)&7;
            if(type==6) { if(loop && cursor>1) { clear(); for(auto& c:channels)c=Channel{}; cursor=0; } else { stop(); return; } }
            else if(type==0) { if(!byte(a)||a>127){stop();return;} noteOff(ch,a); }
            else if(type==1) { if(!byte(a)){stop();return;} if(a&128){if(!byte(b)||b>127){stop();return;}channels[ch].velocity=b;} noteOn(ch,a&127,channels[ch].velocity); }
            else if(type==2) { if(!byte(a)){stop();return;} channels[ch].bend=a; for(int i=0;i<9;i++)if(voices[i].channel==ch)pitch(i); }
            else if(type==3) { if(!byte(a)||a<10||a>14){stop();return;}control(ch,a,0); }
            else if(type==4) { if(!byte(a)||!byte(b)||a>9||b>127){stop();return;}control(ch,a,b); }
            else { stop(); return; }
            if(e&128) {
                uint32_t ticks=0; bool ended=false;
                for(int j=0;j<4;j++) { if(!byte(a)){stop();return;} ticks=(ticks<<7)|(a&127); if(!(a&128)){ended=true;break;} }
                if(!ended){stop();return;} if(ticks){delay=ticks;return;}
            }
        }
        stop();
    }
public:
    bool init(const uint8_t* p,size_t n) {
        stop(); ready=false;
        if(!p||n<8+sizeof(bank)||memcmp(p,"#OPL_II#",8))return false;
        memcpy(bank,p+8,sizeof(bank));
        OPL::DOSBox::DBOPL::InitTables();
        chip.~Chip(); new (&chip) Chip(); // engine-thread reset, placement only; no heap
        chip.Setup(kRate); ready=true; reg(1,32);
        for(int i=0;i<12*256;i++) {
            double hz=440.0*std::pow(2.0,(double(i)/256.0-69.0)/12.0);
            double f=hz*1048576.0/49716.0;
            frequencies[i]=uint16_t(clamp(int(f+0.5),1023));
        }
        ready=true;return true;
    }
    void play(const Song* s,bool looping) { stop(); if(!ready||!s)return; song=s;cursor=delay=phase=serial=0;loop=looping;playing=true;paused=false;for(auto& c:channels)c=Channel{}; }
    void stop() { clear();playing=false;paused=false;song=nullptr; }
    void pause(bool value){paused=value;}
    bool isPlaying()const{return playing;}
    bool uses(const Song* s)const{return song==s;}
    void setVolume(int v){volume=clamp(v,127);}
    void renderAdd(int32_t* out,size_t count) {
        for(size_t i=0;i<count;i++) {
            if(!playing||paused)continue;
            if(!delay)events();
            if(!playing)continue;
            int32_t sample=0; chip.GenerateBlock2(1,&sample);
            out[i]+=sample*volume/127;
            phase+=140; if(phase>=kRate){phase-=kRate;if(delay)--delay;}
        }
    }
};
} // namespace doom_audio
