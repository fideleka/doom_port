//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//  Sky rendering. The DOOM sky is a texture map like any
//  wall, wrapping around. A 1024 columns equal 360 degrees.
//  The default sky map is 256 columns and repeats 4 times
//  on a 320 screen?
//  
//



// Needed for FRACUNIT.
#include "m_fixed.h"

// Needed for Flat retrieval.
#include "r_data.h"


#include "r_sky.h"
#include "r_draw.h"
#include "r_main.h"
#include "i_video.h"

//
// sky mapping
//
int			skyflatnum;
int			skytexture;
int			skytexturemid;



//
// R_InitSkyMap
// Called whenever the view size changes.
//
void R_InitSkyMap (void)
{
  // skyflatnum = R_FlatNumForName ( SKYFLATNAME );
    skytexturemid = 100*FRACUNIT;
}


// Preserve the original 168-row sky angular span at the port's 280-column
// full viewport. Wall and weapon projection deliberately remain unchanged.
fixed_t R_SkyScale(void)
{
    return (fixed_t)(((int64_t)FRACUNIT * (SCREENHEIGHT_UI - 32) * 280)
                     / ((SCREENHEIGHT - 32) * scaledviewwidth));
}

// Sky columns wrap by their actual asset height, not the wall drawer's
// hardcoded 128 mask. Negative coordinates use floor-modulo as well.
void R_DrawSkyColumn(void)
{
    int count = dc_yh - dc_yl;
    int height = textureheight[skytexture] >> FRACBITS;
    int x = dc_x << detailshift;
    uint32_t period, frac, step;
    int64_t initial;
    byte* dest;
    if (count < 0 || height <= 0)
        return;
    period = (uint32_t)height << FRACBITS;
    initial = ((int64_t)dc_texturemid + (dc_yl - centery) * (int64_t)dc_iscale) % period;
    if (initial < 0)
        initial += period;
    frac = (uint32_t)initial;
    initial = (int64_t)dc_iscale % period;
    if (initial < 0)
        initial += period;
    step = (uint32_t)initial;
    dest = ylookup[dc_yl] + columnofs[x];
    do {
        *dest = dc_colormap[dc_source[frac >> FRACBITS]];
        if (detailshift)
            *(dest + columnofs[x + 1] - columnofs[x]) = *dest;
        dest += SCREENWIDTH;
        frac += step;
        if (frac >= period)
            frac -= period;
    } while (count--);
}
