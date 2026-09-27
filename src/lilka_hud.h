#pragma once
#include <stdint.h>

// Active content is confined to x=12..267, y<=231 on the rounded 280x240 panel.
struct LilkaHudState {
    int ammo, health, armor;
    uint16_t owned;
    uint8_t ready, keys;
};

namespace lilka_hud {
constexpr uint16_t black=0, shadow=0x2104, edge=0x94B2;
constexpr uint16_t dim=0x632C, bone=0xD69A, gold=0xFDC0;
constexpr uint16_t blood=0xC841, steel=0x94D7;
inline uint16_t rgb565(uint32_t p) {
    return uint16_t(((p>>19)&31)<<11 | ((p>>10)&63)<<5 | ((p>>3)&31));
}
inline void bar(uint16_t* row,int y,int x,int top,int w,int h,uint16_t color) {
    if(y<top||y>=top+h) return;
    for(int i=x;i<x+w;++i) row[i]=color;
}
inline const uint16_t* weaponShape(int index) {
    static const uint16_t shapes[9][7] = {
        {0x0780,0x1FE0,0x3FF0,0x3FF0,0x1FE0,0x0FC0,0x0780},
        {0,0x3FF0,0x7FF8,0x7FF8,0x1FE0,0x0FC0,0x0FC0},
        {0,0x1FE0,0x7FF8,0x7FF8,0x01C0,0x0380,0x0700},
        {0,0x7FFE,0x7FFE,0x1FF0,0x0380,0x0700,0x0E00},
        {0x7FFE,0x7FFE,0x7FFE,0x1FF0,0x0380,0x0700,0x0E00},
        {0,0x7FFE,0x7FFE,0x3FFC,0x07C0,0x0F80,0x1F00},
        {0,0x7FFC,0x7FFE,0x7FFC,0x1FF0,0x0380,0x0700},
        {0,0x3FFC,0x7FFE,0x7FFE,0x1FF0,0x0700,0x0E00},
        {0x0FF0,0x3FFC,0x7FFE,0x7FFE,0x3FFC,0x0FF0,0x0380},
    };
    return shapes[index];
}
// Cartridge, medkit, and shield: a shared outlined/shaded pixel-art style.
inline const char* iconRow(int icon,int y) {
    static const char* shapes[3][11] = {
        {"...111.....","..12221....","..12221....","..12221....",
         "..12221....","..12221....","..12221....","..12221....",
         "..12221....","..13331....","...111....."},
        {"...........","..1111111..","..1222221..","..1223221..",
         "..1233321..","..1233321..","..1233321..","..1223221..",
         "..1222221..","..1111111..","..........."},
        {"..1111111..","..1222221..","..1233321..","..1233321..",
         "..1233321..","...23332...","...23332...","....232....",
         "....232....",".....3.....","..........."},
    };
    return shapes[icon][y];
}
inline void drawIcon(uint16_t* row,int y,int x,int top,int icon) {
    if(y<top||y>=top+11) return;
    const char* shape=iconRow(icon,y-top);
    const uint16_t main=icon==0?gold:icon==1?blood:steel;
    for(int i=0;i<11;++i)
        if(shape[i]!='.') row[x+i]=shape[i]=='1'?shadow:shape[i]=='2'?bone:main;
}
// Source frame contains the authentic red Doom STTNUM glyphs. Copy their
// foreground pixels only; the new background remains uninterrupted.
inline void copyInk(uint16_t* row,int y,int destX,int destTop,int sourceX,
                    int sourceTop,int width,int height,const uint32_t* frame) {
    if(y<destTop||y>=destTop+height) return;
    const int sy=sourceTop+y-destTop;
    for(int i=0;i<width;++i) {
        const int sx=sourceX+i;
        const uint16_t pixel=rgb565(frame[sy*320+sx]);
        if(pixel!=ST_HudBackground565(sx,sy-208)) row[destX+i]=pixel;
    }
}
inline void renderRow(uint16_t* row,int y,int width,
                      const LilkaHudState& state,const uint32_t* frame) {
    for(int x=0;x<width;++x) row[x]=black;
    if(y<184||y>=232) return;
    // Unlettered stone from the original STBAR WAD art. Its static AMMO,
    // HEALTH and ARMOR labels live lower in the patch and are not repeated.
    for(int x=12;x<268;++x)
        row[x]=ST_HudBackground565(48+(x-12)%56,(y-184)%23);
    if(y==199) bar(row,y,12,199,256,1,edge);
    if(y<200) {
        static const char keys[9]={'1','1','2','3','3','4','5','6','7'};
        static const uint8_t nums[7][5]={{2,6,2,2,7},{7,1,7,4,7},
            {7,1,7,1,7},{5,5,7,1,1},{7,4,7,1,7},
            {7,4,7,5,7},{7,1,1,1,1}};
        for(int i=0;i<9;++i) {
            const int x=23+i*26;
            const bool selected=state.ready==i;
            const uint16_t ink=!(state.owned&(1<<i))?dim:selected?gold:bone;
            bar(row,y,x,185,24,14,selected?gold:shadow);
            bar(row,y,x+1,186,22,12,shadow);
            const int shapeY=y-188;
            if(shapeY>=0&&shapeY<7) {
                const uint16_t bits=weaponShape(i)[shapeY];
                for(int k=0;k<15;++k)
                    if(bits&(1<<(14-k))) row[x+3+k]=ink;
            }
            if(y>=191&&y<196)
                for(int b=0;b<3;++b)
                    if(nums[keys[i]-'1'][y-191]&(4>>b)) row[x+19+b]=ink;
        }
        return;
    }
    drawIcon(row,y,15,209,0);
    drawIcon(row,y,73,209,1);
    drawIcon(row,y,164,209,2);
    if(state.ammo<0) {
        bar(row,y,49,221,7,2,blood);
        bar(row,y,60,221,7,2,blood);
    } else copyInk(row,y,28,209,0,211,45,17,frame);
    copyInk(row,y,83,209,48,211,43,17,frame);
    copyInk(row,y,180,209,179,211,43,17,frame);
    // Full-size, genuinely animated portrait, centered at physical x=140.
    bar(row,y,125,200,30,31,shadow);
    bar(row,y,127,201,26,29,edge);
    if(y>=201&&y<230) {
        const int sy=208+y-201;
        for(int i=0;i<24;++i) row[128+i]=rgb565(frame[sy*320+143+i]);
    }
    // Real STKEYS card/skull art, stacked as on the DOS status bar.
    for(int key=0;key<3;++key) {
        if(!(state.keys&(1<<key))) continue;
        const int top=202+key*9;
        if(y<top||y>=top+7) continue;
        const int sy=211+key*10+(y-top)*5/7;
        for(int i=0;i<12;++i) {
            const int sx=239+i*8/12;
            const uint16_t pixel=rgb565(frame[sy*320+sx]);
            if(pixel!=ST_HudBackground565(sx,sy-208)) row[244+i]=pixel;
        }
    }
}
} // namespace lilka_hud
