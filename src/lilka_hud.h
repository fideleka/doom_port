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
    if(y<top||y>=top+16) return;
    const char* shape=iconRow(icon,(y-top)*11/16);
    const uint16_t main=icon==0?gold:icon==1?blood:steel;
    for(int i=0;i<16;++i)
        if(shape[i*11/16]!='.') {
            const char pixel=shape[i*11/16];
            row[x+i]=pixel=='1'?shadow:pixel=='2'?bone:main;
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
inline void drawPatch(uint16_t* row,int y,int x,int top,
                      int width,int height,int kind,int index) {
    if(y<top||y>=top+height) return;
    const int sourceWidth=ST_HudPatchWidth(kind,index);
    const int sourceHeight=ST_HudPatchHeight(kind,index);
    if(!sourceWidth||!sourceHeight) return;
    const int sy=(y-top)*sourceHeight/height;
    for(int i=0;i<width;++i) {
        const int color=ST_HudPatchPixel(kind,index,i*sourceWidth/width,sy);
        if(color>=0) row[x+i]=uint16_t(color);
    }
}
inline void drawOriginalPatch(uint16_t* row,int y,int x,int top,
                              int kind,int index) {
    const int width=ST_HudPatchWidth(kind,index);
    const int height=ST_HudPatchHeight(kind,index);
    if(y<top||y>=top+height) return;
    const int drawX=x-ST_HudPatchLeftOffset(kind,index);
    for(int i=0;i<width;++i) {
        const int color=ST_HudPatchPixel(kind,index,i,y-top);
        if(color>=0) row[drawX+i]=uint16_t(color);
    }
}
inline void drawValue(uint16_t* row,int y,int left,int value,bool percent) {
    if(value<0) return;
    if(value>999) value=999;
    int digits[3],count=0;
    do { digits[count++]=value%10; value/=10; } while(value&&count<3);
    int x=left;
    for(int i=count-1;i>=0;--i) {
        drawOriginalPatch(row,y,x,208,2,digits[i]);
        x+=14; // STlib_drawNum uses STTNUM0's 14-pixel cell for every digit.
    }
    if(percent) drawOriginalPatch(row,y,x,208,3,0);
}
inline void renderRow(uint16_t* row,int y,int width,
                      const LilkaHudState& state) {
    for(int x=0;x<width;++x) row[x]=black;
    if(y<184||y>=232) return;
    // One continuous stone field: no repeated STBAR columns or gray boxes.
    const uint16_t stoneBase=ST_HudBackground565(70,8);
    for(int x=12;x<268;++x)
        row[x]=stone(x,y,stoneBase);
    if(y==199) bar(row,y,12,199,256,1,edge);
    if(y<200) {
        static const char keys[9]={'1','1','2','3','3','4','5','6','7'};
        static const uint8_t nums[7][5]={{2,6,2,2,7},{7,1,7,4,7},
            {7,1,7,1,7},{5,5,7,1,1},{7,4,7,1,7},
            {7,4,7,5,7},{7,1,1,1,1}};
        for(int i=0;i<9;++i) {
            const int x=23+i*26;
            const bool selected=state.ready==i;
            const bool owned=state.owned&(1<<i);
            const uint16_t ink=!owned?dim:selected?gold:bone;
            const uint16_t border=selected?gold:dim;
            bar(row,y,x,185,24,1,border);
            bar(row,y,x,198,24,1,border);
            bar(row,y,x,185,1,14,border);
            bar(row,y,x+23,185,1,14,border);
            const int shapeY=y-187;
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
    // Same horizontal composition as Doom's original bar, omitting only its
    // rightmost all-ammo table and moving ARMS into the row above.
    for(int divider : {84,157,230,255}) {
        bar(row,y,divider,200,1,32,shadow);
    }
    drawIcon(row,y,12,208,0);
    drawIcon(row,y,85,208,1);
    drawIcon(row,y,158,208,2);
    if(state.ammo<0) {
        bar(row,y,34,215,6,2,blood);
        bar(row,y,45,215,6,2,blood);
    } else drawValue(row,y,30,state.ammo,false);
    drawValue(row,y,101,state.health,true);
    drawValue(row,y,174,state.armor,true);

    // Full transparent Doomguy patch immediately left of the narrow keys.
    const int fw=ST_HudPatchWidth(1,state.face);
    const int fh=ST_HudPatchHeight(1,state.face);
    const int fx=243-fw/2, fy=216-fh/2;
    if(y>=fy&&y<fy+fh)
        for(int x=0;x<fw;++x) {
            const int color=ST_HudPatchPixel(1,state.face,x,y-fy);
            if(color>=0) row[fx+x]=uint16_t(color);
        }

    // The original key column was gray stone, not three colored outlines.
    // Each key gets a tiny beveled square; collected art supplies its color.
    for(int key=0;key<3;++key) {
        const int top=201+key*10;
        const bool collected=state.keyTypes[key]!=255;
        bar(row,y,257,top,10,1,edge);
        bar(row,y,257,top+8,10,1,shadow);
        bar(row,y,257,top,1,9,edge);
        bar(row,y,266,top,1,9,shadow);
        if(!collected) {
            bar(row,y,261,top+3,2,3,dim);
            continue;
        }
        const int index=state.keyTypes[key];
        drawPatch(row,y,258,top+1,8,7,0,index);
    }
}
} // namespace lilka_hud
