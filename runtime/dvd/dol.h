#ifndef MGS_DOL_H
#define MGS_DOL_H

#include "../memory/guest.h"
#include "disc.h"
#include <stdlib.h>

typedef struct MgsDolSection {
    uint32_t address, size;
    int      is_text;
} MgsDolSection;

typedef struct MgsDolInfo {
    MgsDolSection sections[18];
    unsigned      section_count;
    uint32_t      bss_address, bss_size;
    uint32_t      entry_point;
    uint32_t      loaded_bytes;
} MgsDolInfo;

int mgs_dol_load(GuestMemory* mem, const void* data, size_t size, MgsDolInfo* info);
int mgs_dol_load_from_disc(GuestMemory* mem, MgsDisc* disc, MgsDolInfo* info);

#endif
