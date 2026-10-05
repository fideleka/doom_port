#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "r_local.h"
#include "r_sky.h"
#include "i_video.h"

extern int skyflatnum;
void R_ExecuteSetViewSize(void);

static byte framebuffer[SCREENWIDTH * SCREENHEIGHT];
static byte maps[32 * 256];
static byte* column;
static fixed_t heights[1];
byte* I_VideoBuffer = framebuffer;
lighttable_t* colormaps = maps;
fixed_t* textureheight = heights;
short screenheightarray[SCREENWIDTH];
int firstflat, *flattranslation;
fixed_t pspritescale, pspriteiscale;
static drawseg_t fixture_drawsegs[MAXDRAWSEGS];
drawseg_t* drawsegs = fixture_drawsegs;
drawseg_t* ds_p = fixture_drawsegs;
int leveltime;
byte* R_GetColumn(int texture, int angle) { (void)texture; (void)angle; return column; }
void* ps_malloc(size_t size) { return malloc(size); }
void DG_printf(const char* fmt, ...) { (void)fmt; }
void I_Error(const char* fmt, ...) { (void)fmt; abort(); }
void* W_CacheLumpNum(int n, int tag) { (void)n; (void)tag; return column; }
void W_ReleaseLumpNum(int n) { (void)n; }

static int floor_sample(int64_t f, int height) {
    int64_t n = f / FRACUNIT;
    if (f < 0 && f % FRACUNIT) --n;
    int v = (int)(n % height);
    return v < 0 ? v + height : v;
}
static void columns(void) {
    for (int h = 64; h <= 192; h += 64) {
        free(column); column=malloc(h); assert(column);
        heights[0] = h * FRACUNIT;
        for (int i = 0; i < h; ++i) column[i] = (byte)i;
        for (int detail = 0; detail < 2; ++detail) {
            R_SetViewSize(10, detail); R_ExecuteSetViewSize();
            assert(viewheight == 208 && scaledviewwidth == 280);
            assert(viewwindowx == 20 && viewwindowy == 0 && centery == 104);
            R_InitSkyMap();
            assert(skytexturemid == 100 * FRACUNIT);
            dc_colormap = maps; dc_source = column; dc_x = 5;
            for (int scale = 0; scale < 4; ++scale) {
                dc_iscale = scale == 0 ? R_SkyScale() : scale == 1 ? FRACUNIT * 3 / 2 : scale == 2 ? -FRACUNIT * 7 / 4 : FRACUNIT * (h + 3);
                dc_texturemid = scale == 0 ? skytexturemid : -5 * FRACUNIT + 17;
                dc_yl = 0; dc_yh = 207;
                memset(framebuffer, 255, sizeof(framebuffer));
                R_DrawSkyColumn();
                for (int y = 0; y < 208; ++y) {
                    int sample = floor_sample((int64_t)dc_texturemid + (y-centery)*(int64_t)dc_iscale, h);
                    int x = viewwindowx + (dc_x << detail);
                    assert(framebuffer[y*320+x] == sample);
                    if (detail) assert(framebuffer[y*320+x+1] == sample);
                    assert(framebuffer[y*320+x-1] == 255);
                }
            }
        }
    }
}
static void planes(void) {
    extern visplane_t visplanes[];
    extern visplane_t* lastvisplane;
    free(column); column=malloc(128); assert(column);
    heights[0] = 128 * FRACUNIT;
    for (int i = 0; i < 128; ++i) column[i] = (byte)i;
    skyflatnum = 7;
    for (int blocks = 5; blocks <= 11; ++blocks) for (int detail = 0; detail < 2; ++detail) {
        R_SetViewSize(blocks, detail); R_ExecuteSetViewSize(); R_InitSkyMap();
        assert(ylookup[0] == framebuffer + viewwindowy * 320);
        assert(columnofs[0] == viewwindowx);
        memset(framebuffer, 255, sizeof(framebuffer));
        memset(visplanes, 0, sizeof(visplane_t));
        visplanes[0].picnum = skyflatnum;
        visplanes[0].minx = 0; visplanes[0].maxx = viewwidth-1;
        for (int x = 0; x < viewwidth; ++x) { visplanes[0].top[x] = 0; visplanes[0].bottom[x] = viewheight-1; }
        lastvisplane = visplanes + 1;
        R_DrawPlanes();
        for (int y = 0; y < viewheight; ++y) for (int x = 0; x < scaledviewwidth; ++x) {
            int sample = floor_sample((int64_t)skytexturemid + (y-centery)*(int64_t)R_SkyScale(),128);
            assert(framebuffer[(viewwindowy+y)*320+viewwindowx+x] == sample);
        }
        // Full-size top is row 16 of the original 128-row sky, not row 109
        // wrapped from -19. The old mapping produces the reported ~18px seam.
        if (blocks >= 10) {
            assert(framebuffer[viewwindowx] == 16);
            assert(floor_sample((int64_t)skytexturemid-centery*(int64_t)(pspriteiscale>>detail),128) == 109);
            for (int y = 0; y < 40; ++y) assert(framebuffer[y*320+viewwindowx] < 50);
        }
    }
}
static void indoors(void) {
    R_SetViewSize(10,0); R_ExecuteSetViewSize();
    for (int i = 0; i < 128; ++i) column[i] = (byte)i;
    dc_colormap=maps;dc_source=column;dc_x=3;dc_yl=0;dc_yh=207;
    dc_texturemid=100*FRACUNIT;dc_iscale=pspriteiscale;
    R_DrawColumn();
    for(int y=0;y<208;++y) assert(framebuffer[y*320+23]==floor_sample((int64_t)dc_texturemid+(y-centery)*(int64_t)dc_iscale,128));
    // Empty sky plane must leave all existing indoor wall pixels untouched.
    extern visplane_t visplanes[]; extern visplane_t* lastvisplane;
    byte saved[sizeof(framebuffer)];memcpy(saved,framebuffer,sizeof(saved));
    visplanes[0].minx=1;visplanes[0].maxx=0;lastvisplane=visplanes+1;
    R_DrawPlanes();assert(memcmp(saved,framebuffer,sizeof(saved))==0);
}
int main(void) {
    for(int m=0;m<32;++m)for(int i=0;i<256;++i)maps[m*256+i]=(byte)i;
    assert(R_AllocMain()>0);
    columns();planes();indoors();R_FreeMain();free(column);
    puts("Production R_ExecuteSetViewSize/R_InitBuffer/R_DrawPlanes/R_DrawSkyColumn: seam, offsets, detail, actual texture bounds/wrap, unchanged indoor walls PASS");
    return 0;
}
