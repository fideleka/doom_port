// Host preview of the exact Lilka HUD row renderer, using real Doom WAD art.
// Usage: hud_preview IWAD.WAD output.ppm [ammo health armor]
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

struct Patch { int width=0, height=0; std::vector<int> pixels; };
static std::vector<uint8_t> wad;
static std::map<std::string,std::pair<size_t,size_t>> directory;
static std::map<std::string,Patch> patches;
static const uint8_t* palette=nullptr;
static int little16(const uint8_t* p) { return p[0]|(p[1]<<8); }
static uint32_t little32(const uint8_t* p) {
    return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
static std::pair<const uint8_t*,size_t> lump(const std::string& name) {
    auto it=directory.find(name);
    if(it==directory.end()) throw std::runtime_error("Missing WAD lump: "+name);
    return {wad.data()+it->second.first,it->second.second};
}
static void loadWad(const char* path) {
    std::ifstream in(path,std::ios::binary);
    if(!in) throw std::runtime_error("Cannot open IWAD");
    wad.assign(std::istreambuf_iterator<char>(in),{});
    if(wad.size()<12 || std::memcmp(wad.data()+1,"WAD",3))
        throw std::runtime_error("Not a WAD file");
    const uint32_t count=little32(wad.data()+4), offset=little32(wad.data()+8);
    if(uint64_t(offset)+uint64_t(count)*16>wad.size()) throw std::runtime_error("Bad WAD directory");
    for(uint32_t i=0;i<count;++i) {
        const uint8_t* entry=wad.data()+offset+i*16;
        const uint32_t pos=little32(entry),size=little32(entry+4);
        if(uint64_t(pos)+size>wad.size()) throw std::runtime_error("Bad lump bounds");
        std::string name(reinterpret_cast<const char*>(entry+8),8);
        const size_t end=name.find('\0');
        if(end!=std::string::npos) name.resize(end);
        directory[name]={pos,size};
    }
    auto pal=lump("PLAYPAL");
    if(pal.second<768) throw std::runtime_error("Bad PLAYPAL");
    palette=pal.first;
}
static Patch decode(const std::string& name) {
    auto data=lump(name);
    const uint8_t* p=data.first;
    if(data.second<8) throw std::runtime_error("Short patch: "+name);
    Patch out;
    out.width=little16(p); out.height=little16(p+2);
    if(out.width<=0||out.height<=0||out.width>2048||out.height>2048||
       data.second<8+size_t(out.width)*4) throw std::runtime_error("Bad patch: "+name);
    out.pixels.assign(size_t(out.width)*out.height,-1);
    for(int x=0;x<out.width;++x) {
        size_t offset=little32(p+8+x*4);
        while(offset<data.second && p[offset]!=255) {
            if(offset+4>data.second) throw std::runtime_error("Bad patch post");
            const int top=p[offset],length=p[offset+1];
            if(offset+size_t(length)+4>data.second) throw std::runtime_error("Bad patch pixels");
            for(int y=0;y<length;++y) if(top+y<out.height)
                out.pixels[size_t(top+y)*out.width+x]=p[offset+3+y];
            offset+=length+4;
        }
    }
    return out;
}
static Patch& patch(int kind,int index) {
    std::string name;
    switch(kind) {
        case 0: name="STKEYS"+std::to_string(index); break;
        case 1: name="STFST00"; break; // representative neutral animated face
        case 2: name="STTNUM"+std::to_string(index); break;
        case 3: name="STTPRCNT"; break;
        default: throw std::runtime_error("Bad patch kind");
    }
    auto found=patches.find(name);
    if(found==patches.end()) found=patches.emplace(name,decode(name)).first;
    return found->second;
}
static uint16_t palette565(int index) {
    if(index<0) return 0;
    const int r=palette[index*3],g=palette[index*3+1],b=palette[index*3+2];
    return uint16_t(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
}
uint16_t ST_HudBackground565(int x,int y) {
    Patch& p=patches.at("STBAR");
    if(x<0||y<0||x>=p.width||y>=p.height) return 0;
    return palette565(p.pixels[size_t(y)*p.width+x]);
}
int ST_HudPatchWidth(int kind,int index) { return patch(kind,index).width; }
int ST_HudPatchHeight(int kind,int index) { return patch(kind,index).height; }
int ST_HudPatchPixel(int kind,int index,int x,int y) {
    Patch& p=patch(kind,index);
    if(x<0||y<0||x>=p.width||y>=p.height) return -1;
    const int ink=p.pixels[size_t(y)*p.width+x];
    return ink<0?-1:palette565(ink);
}
#include "lilka_hud.h"
int main(int argc,char** argv) {
    try {
        if(argc<3) throw std::runtime_error("Usage: hud_preview IWAD.WAD output.ppm [ammo health armor]");
        loadWad(argv[1]);
        patches.emplace("STBAR",decode("STBAR"));
        LilkaHudState state{};
        state.ammo=argc>3?std::atoi(argv[3]):6;
        state.health=argc>4?std::atoi(argv[4]):101;
        state.armor=argc>5?std::atoi(argv[5]):100;
        state.owned=(1<<0)|(1<<2)|(1<<3);
        state.ready=3;
        state.face=0;
        for(auto& key:state.keyTypes) key=255;
        std::ofstream out(argv[2],std::ios::binary);
        out<<"P6\n280 240\n255\n";
        for(int y=0;y<240;++y) {
            uint16_t row[280]={};
            if(y<184) {
                // Neutral game viewport; the HUD below uses exact firmware code.
                for(int x=0;x<280;++x) row[x]=0x1082;
            } else lilka_hud::renderRow(row,y,280,state);
            for(uint16_t color:row) {
                const char rgb[3]={char(((color>>11)&31)*255/31),
                                   char(((color>>5)&63)*255/63),
                                   char((color&31)*255/31)};
                out.write(rgb,3);
            }
        }
    } catch(const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
