#include "guest.h"
#include <stdlib.h>

int guest_memory_init(GuestMemory* m)
{
    /* calloc, not malloc: the SDK's own boot code assumes cleared memory in
     * places, and an uninitialised read here would differ between runs, which
     * would make the frame-by-frame comparison against Dolphin useless.
     */
    m->ram  = (uint8_t*)calloc(1, GUEST_RAM_SIZE);
    m->aram = (uint8_t*)calloc(1, GUEST_ARAM_SIZE);
    m->vmem = (uint8_t*)calloc(1, GUEST_VMEM_SIZE);
    if (!m->ram || !m->aram || !m->vmem) {
        guest_memory_free(m);
        return 0;
    }
    return 1;
}

void guest_memory_free(GuestMemory* m)
{
    free(m->ram);  m->ram = NULL;
    free(m->aram); m->aram = NULL;
    free(m->vmem); m->vmem = NULL;
}
