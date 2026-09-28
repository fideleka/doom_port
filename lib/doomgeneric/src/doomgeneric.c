#include <stdio.h>

#include "m_argv.h"

#include "doomgeneric.h"
#include "d_alloc.h"

uint16_t* DG_ScreenBuffer = 0;

void M_FindResponseFile(void);
void D_DoomMain (void);


void doomgeneric_Create(int argc, char **argv)
{
	// save arguments
    myargc = argc;
    myargv = argv;

	M_FindResponseFile();

	DG_ScreenBuffer = malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY * sizeof(*DG_ScreenBuffer));

	DG_Init();

	D_DoomMain ();
}
