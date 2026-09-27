#pragma once
#include <stdint.h>

// Active content is confined to x=12..267, y<=231 on the rounded 280x240 panel.
struct LilkaHudState {
    int ammo, health, armor;
    uint16_t owned;
    uint8_t ready, face;
    uint8_t keyTypes[3]; // 0..5: card/skull art, 255: not collected
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
    if(y<top||y>=top+22) return;
    const char* shape=iconRow(icon,(y-top)/2);
    const uint16_t main=icon==0?gold:icon==1?blood:steel;
    for(int i=0;i<11;++i)
        if(shape[i]!='.') {
            const uint16_t color=shape[i]=='1'?shadow:shape[i]=='2'?bone:main;
            row[x+i*2]=color;
            row[x+i*2+1]=color;
        }
}
inline uint16_t stone(int x,int y,uint16_t base) {
    const unsigned n=(unsigned(x)*37u+unsigned(y)*61u+
                      unsigned(x*y)*13u)^(unsigned(x)*unsigned(y+7));
    const int shade=int((n^(n>>5))&7)-3;
    const int r=(base>>11)&31, g=(base>>5)&63, b=base&31;
    const int rr=r+shade/2, gg=g+shade, bb=b+shade/2;
    return uint16_t(((rr<0?0:rr>31?31:rr)<<11) |
                    ((gg<0?0:gg>63?63:gg)<<5) |
                    (bb<0?0:bb>31?31:bb));
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
    if(y<174||y>=232) return;
    // One continuous stone field: no repeated STBAR columns or gray boxes.
    const uint16_t stoneBase=ST_HudBackground565(70,8);
    for(int x=12;x<268;++x)
        row[x]=stone(x,y,stoneBase);
    if(y==189) bar(row,y,12,189,256,1,edge);
    if(y<190) {
        static const char keys[9]={'1','1','2','3','3','4','5','6','7'};
        static const uint8_t nums[7][5]={{2,6,2,2,7},{7,1,7,4,7},
            {7,1,7,1,7},{5,5,7,1,1},{7,4,7,1,7},
            {7,4,7,5,7},{7,1,1,1,1}};
        for(int i=0;i<9;++i) {
            const int x=23+i*26;
            const bool selected=state.ready==i;
            const bool owned=state.owned&(1<<i);
            const uint16_t ink=!owned?dim:selected?gold:bone;
            const uint16_t border=selected?gold:owned?dim:shadow;
            bar(row,y,x,175,24,1,border);
            bar(row,y,x,188,24,1,border);
            bar(row,y,x,175,1,14,border);
            bar(row,y,x+23,175,1,14,border);
            const int shapeY=y-177;
            if(shapeY>=0&&shapeY<7) {
                const uint16_t bits=weaponShape(i)[shapeY];
                for(int k=0;k<15;++k)
                    if(bits&(1<<(14-k))) row[x+3+k]=ink;
            }
            if(y>=181&&y<186)
                for(int b=0;b<3;++b)
                    if(nums[keys[i]-'1'][y-181]&(4>>b)) row[x+19+b]=ink;
        }
        return;
    }
    drawIcon(row,y,27,192,0);
    drawIcon(row,y,94,192,1);
    drawIcon(row,y,185,192,2);
    if(state.ammo<0) {
        bar(row,y,48,222,7,2,blood);
        bar(row,y,59,222,7,2,blood);
    } else copyInk(row,y,25,215,0,211,45,17,frame);
    copyInk(row,y,84,215,48,211,43,17,frame);
    copyInk(row,y,175,215,179,211,43,17,frame);

    // Decode the face patch's transparency instead of cropping the old bar's
    // rectangular face region. Its full dimensions are centered on x=140.
    const int fw=ST_HudPatchWidth(1,state.face);
    const int fh=ST_HudPatchHeight(1,state.face);
    const int fx=140-fw/2, fy=211-fh/2;
    if(y==fy-1||y==fy+fh) bar(row,y,fx-1,y,fw+2,1,shadow);
    if(y>=fy-1&&y<=fy+fh) {
        row[fx-1]=shadow;
        row[fx+fw]=shadow;
    }
    if(y>=fy&&y<fy+fh)
        for(int x=0;x<fw;++x) {
            const int color=ST_HudPatchPixel(1,state.face,x,y-fy);
            if(color>=0) row[fx+x]=uint16_t(color);
        }

    // Three key slots remain visible when empty; collected slots display
    // the authentic card/skull patch, doubled and centered in the slot.
    static const uint16_t keyColor[3]={0x5C7F,0xFDC0,0xF986};
    for(int key=0;key<3;++key) {
        const int top=194+key*11;
        const bool collected=state.keyTypes[key]!=255;
        const uint16_t border=keyColor[key];
        bar(row,y,241,top,20,1,border);
        bar(row,y,241,top+9,20,1,border);
        bar(row,y,241,top,1,10,border);
        bar(row,y,260,top,1,10,border);
        if(!collected) {
            bar(row,y,249,top+3,4,3,shadow);
            continue;
        }
        const int index=state.keyTypes[key];
        const int kw=ST_HudPatchWidth(0,index);
        const int kh=ST_HudPatchHeight(0,index);
        if(y<top+1||y>=top+9||!kw||!kh) continue;
        const int sy=(y-top-1)*kh/8;
        for(int x=0;x<16;++x) {
            const int color=ST_HudPatchPixel(0,index,x*kw/16,sy);
            if(color>=0) row[243+x]=uint16_t(color);
        }
    }
}
} // namespace lilka_hud
