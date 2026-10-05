#include <stdio.h>

#include "m_argv.h"

#include "doomgeneric.h"
#include "d_alloc.h"
#include "i_system.h"

uint16_t* DG_ScreenBuffer = 0;

void M_FindResponseFile(void);
void D_DoomMain (void);


void doomgeneric_Create(int argc, char **argv)
{
	// save arguments
    myargc = argc;
    myargv = argv;

	M_FindResponseFile();

    // Embedded startup may reserve this before creating its gated tasks.
    if (!DG_ScreenBuffer)
        DG_ScreenBuffer = (uint16_t*) malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY * sizeof(*DG_ScreenBuffer));
    if (!DG_ScreenBuffer)
        I_Error("Failed to allocate Doom framebuffer");

    DG_Init();

    D_DoomMain ();
}
