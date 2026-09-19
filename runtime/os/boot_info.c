/* The low-memory globals the boot ROM leaves behind.
 *
 * A GameCube game does not start from a blank machine. Before main.dol runs,
 * the IPL and the apploader have already filled the first 256 bytes of RAM:
 * the disc ID, how much memory there is, what kind of console this is, the
 * bus and core clocks, and the TV standard. The SDK reads all of it during
 * OSInit and never questions it.
 *
 * Leaving it zeroed is not neutral. Zero is a VALID-LOOKING answer to every
 * one of those questions and the SDK acts on each:
 *
 *   memorySize 0    -> "Memory 0 MB", and every size check against it fails.
 *   consoleType 0   -> OSGetConsoleType returns its unknown-board value,
 *                      0x10000002, which reports as "Development HW-1" - and
 *                      a game that checks for a devkit then takes its devkit
 *                      path, loading its overlay to an address that only
 *                      exists on development hardware.
 *   TV mode 0       -> NTSC, on a PAL disc.
 *   bus/core clock 0-> every timing calculation divides by zero or by luck.
 *
 * So this is not cosmetic. It is the difference between the game believing it
 * is on the hardware it shipped for and believing it is on a debug board.
 */
#include "os_runtime.h"
#include "../dvd/disc.h"

#include <string.h>

/* OSBootInfo, at physical address 0. From the SDK's os.h. */
#define BI_DISK_ID        0x80000000u   /* 32 bytes */
#define BI_MAGIC          0x80000020u
#define BI_VERSION        0x80000024u
#define BI_MEMORY_SIZE    0x80000028u
#define BI_CONSOLE_TYPE   0x8000002Cu
#define BI_ARENA_LO       0x80000030u
#define BI_ARENA_HI       0x80000034u
#define BI_FST_LOCATION   0x80000038u
#define BI_FST_MAX_LENGTH 0x8000003Cu

#define OS_TV_MODE        0x800000CCu
#define OS_SIM_MEM_SIZE   0x800000F0u
#define OS_BI2_ADDRESS    0x800000F4u
#define OS_BUS_CLOCK      0x800000F8u
#define OS_CORE_CLOCK     0x800000FCu

#define BOOT_MAGIC        0x0D15EA5Eu   /* "0DISEASE" - the SDK's own joke */
#define BOOT_VERSION      1u

/* A retail GameCube, 24 MB. OS_CONSOLE_RETAIL2 is what a production console
 * reports; the development values steer the SDK and the game onto debug
 * paths that assume hardware this host does not provide. */
#define CONSOLE_RETAIL2   0x00000002u
#define MEM1_SIZE         0x01800000u

/* 162 MHz bus, 486 MHz core. The timebase is the bus clock over four, which
 * is where MGS_TIMEBASE_HZ comes from - so these two must agree with it. */
#define BUS_CLOCK         162000000u
#define CORE_CLOCK        486000000u

/* TV standard, as VIInit reads it. The region letter in the game ID is what
 * decides it, because that is what decided which disc was pressed. */
#define TV_NTSC  0u
#define TV_PAL   1u
#define TV_MPAL  2u

static unsigned tv_mode_for(const char* game_id)
{
    /* Fourth character of the game code: P/D/F/S/I/H are the European
     * releases, E/J the 60 Hz ones, and Brazil's M-PAL is its own mode. */
    switch (game_id[3]) {
        case 'P': case 'D': case 'F': case 'S': case 'I': case 'H':
        case 'U': case 'X': case 'Y': case 'Z':
            return TV_PAL;
        case 'B':
            return TV_MPAL;
        default:
            return TV_NTSC;
    }
}

void mgs_boot_info_init(GuestMemory* mem, const MgsDisc* disc)
{
    unsigned i;

    if (!mem) return;

    /* The disc ID, exactly where DVDGetCurrentDiskID reads it. Six ID bytes,
     * then the disc number and version, then the DVD magic word at 0x1C -
     * which is the field a game checks to decide a disc is real. */
    for (i = 0; i < 32u; ++i) guest_write8(mem, BI_DISK_ID + i, 0u);
    if (disc && disc->mounted) {
        for (i = 0; i < 6u && disc->game_id[i]; ++i)
            guest_write8(mem, BI_DISK_ID + i, (uint8_t)disc->game_id[i]);
        guest_write8(mem, BI_DISK_ID + 6u, disc->disc_number);
        guest_write32(mem, BI_DISK_ID + 0x1Cu, 0xC2339F3Du);
    }

    guest_write32(mem, BI_MAGIC, BOOT_MAGIC);
    guest_write32(mem, BI_VERSION, BOOT_VERSION);
    guest_write32(mem, BI_MEMORY_SIZE, MEM1_SIZE);
    guest_write32(mem, BI_CONSOLE_TYPE, CONSOLE_RETAIL2);

    /* Zero means "use the linker's __ArenaLo/__ArenaHi", which is what a disc
     * booted without an apploader-supplied arena gets. Inventing values here
     * would override the game's own layout. */
    guest_write32(mem, BI_ARENA_LO, 0u);
    guest_write32(mem, BI_ARENA_HI, 0u);
    guest_write32(mem, BI_FST_LOCATION, 0u);
    guest_write32(mem, BI_FST_MAX_LENGTH, 0u);

    guest_write32(mem, OS_SIM_MEM_SIZE, MEM1_SIZE);
    guest_write32(mem, OS_BI2_ADDRESS, 0u);
    guest_write32(mem, OS_BUS_CLOCK, BUS_CLOCK);
    guest_write32(mem, OS_CORE_CLOCK, CORE_CLOCK);
    guest_write32(mem, OS_TV_MODE,
                  disc && disc->mounted ? tv_mode_for(disc->game_id) : TV_PAL);
}
