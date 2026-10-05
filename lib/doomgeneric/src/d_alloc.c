// DESCRIPTION:
// This file allocates and frees buffers by using heap.
// Its primary purpose is to be used in embedded systems with limited stack space.
// It is not used in the original Doom source code.
// Lilka uses it to utilize PSRAM to store stuff like visplanes.

#include "d_alloc.h"
#include "i_system.h"
#include "r_main.h"
#include "r_plane.h"
#include "r_things.h"
#include "r_bsp.h"

int D_TryAllocBuffers(void) {
    int bytes;
    DG_printf("D_AllocBuffers: Allocating buffers\n");
    bytes = R_AllocMain();
    DG_printf("R_AllocMain: allocated %d bytes\n", bytes);
    if (bytes < 0) goto failed;
    bytes = R_AllocPlanes();
    DG_printf("R_AllocPlanes: allocated %d bytes\n", bytes);
    if (bytes < 0) goto failed;
    bytes = R_AllocThings();
    DG_printf("R_AllocThings: allocated %d bytes\n", bytes);
    if (bytes < 0) goto failed;
    bytes = R_AllocBSP();
    DG_printf("R_AllocBSP: allocated %d bytes\n", bytes);
    if (bytes < 0) goto failed;
    return 1;
failed:
    D_FreeBuffers();
    return 0;
}

void D_AllocBuffers(void) {
    if (!D_TryAllocBuffers()) I_Error("Failed to allocate Doom engine buffers");
}

void D_FreeBuffers() {
    DG_printf("D_FreeBuffers: Freeing buffers\n");
    R_FreeMain();
    R_FreePlanes();
    R_FreeThings();
    R_FreeBSP();
}

// void* ps_malloc(size_t size) {
//     void* ptr = heap_caps_malloc(size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
//     if (ptr == NULL) {
//         DG_printf("ps_malloc: failed to allocate %d bytes\n", size);
//     }
//     return ptr;
// }
