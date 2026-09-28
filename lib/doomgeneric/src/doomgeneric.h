#ifndef DOOM_GENERIC
#define DOOM_GENERIC

#include <stdlib.h>
#include <stdint.h>

#define DOOMGENERIC_RESX 320
#define DOOMGENERIC_RESY 240


extern uint32_t* DG_ScreenBuffer;

void doomgeneric_Create(int argc, char **argv);
void doomgeneric_Tick();


//Implement below functions for your platform
void DG_Init();
void DG_DrawFrame();
void DG_SleepMs(uint32_t ms);
uint32_t DG_GetTicksMs();
uint32_t DG_GetTicksUs();
void DG_PerfFrame(uint32_t simulation_us, uint32_t display_us);
void DG_PerfConvert(uint32_t conversion_us);
void DG_PerfWorld(uint32_t world_us);
int DG_GetKey(int* pressed, unsigned char* key);
void DG_SetWindowTitle(const char * title);

#endif //DOOM_GENERIC
