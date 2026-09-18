#ifndef MGS_HOST_MODULE_H
#define MGS_HOST_MODULE_H

#include <stdint.h>

typedef struct MgsModule {
    void*       handle;
    const void* desc;
    uint32_t    entry_point;
    uint32_t    cpu_state_size;
    char        game_id[8];
    uint32_t    code_ranges, chunk_ranges, rel_modules;
    int       (*dispatch)(void* state, uint32_t address);
    void      (*set_patch_hook)(int (*)(void*, uint32_t));
    char        error[256];
} MgsModule;

int   mgs_module_load(MgsModule* mod, const char* path);
void  mgs_module_unload(MgsModule* mod);
void* mgs_module_new_cpu_state(const MgsModule* mod, uint8_t* ram, uint32_t ram_size);

/* The module's register file and program counter, so the CPU seam can be
 * bound to them and a stop can be reported by address. */
uint32_t* mgs_module_gpr(void* cpu_state);
uint32_t  mgs_module_pc(const void* cpu_state);

#endif
