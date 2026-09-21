#!/usr/bin/env python3
"""Watch guest words in Dolphin, to see what a WORKING run does.

WHY THIS EXISTS. Sixteen links of the movie stall were derived by reading
the binary backwards (F209-F215), and each answer revealed another state
field. The design document makes Dolphin the oracle for divergence, and
"what should this word be" is far cheaper to observe than to derive.

HOW IT READS MEMORY. Dolphin backs emulated RAM with a shared-memory
mapping, so guest memory can be read straight out of the emulator's address
space - no GDB stub (the flatpak build has none compiled in: `strings` finds
no "gdb" at all) and no debugger UI.

WHY IT LAUNCHES DOLPHIN RATHER THAN ATTACHING. `kernel.yama.ptrace_scope` is
1 here, so only an ancestor may read /proc/<pid>/mem. Being a sibling is not
enough, so this has to be the parent.

THE MAPPING. Offset 0 of that region is physical address 0, which is guest
0x80000000, so a guest address is `addr - 0x80000000` within it. The game ID
at offset 0 is the check that the right region was found - if that does not
read as the disc's ID, nothing else here is trustworthy.
"""
import os
import subprocess
import sys
import time

MEM1 = 0x80000000


def emulator_pid():
    """The emulator itself, not the flatpak and bwrap wrappers around it.

    Found by reading /proc rather than with pgrep, for two reasons that both
    bite here. The kernel truncates a process's `comm` to 15 characters, so
    `pgrep -x dolphin-emu-nogui` matches NOTHING - the name it compares
    against is "dolphin-emu-nog". And `pgrep -f dolphin-emu-nogui` matches
    this script's own command line, because the name is in the arguments it
    passes to flatpak, which is the self-match that has cost time in this
    project before.
    """
    me = os.getpid()
    for entry in os.listdir('/proc'):
        if not entry.isdigit() or int(entry) == me:
            continue
        try:
            with open('/proc/%s/comm' % entry) as fh:
                if fh.read().strip() == 'dolphin-emu-nog':
                    return int(entry)
        except OSError:
            continue
    return None


def ram_base(pid):
    """Dolphin's emulated RAM: the shared mapping of at least MEM1's size."""
    try:
        for line in open('/proc/%d/maps' % pid):
            if 'dolphin-emu' not in line or '/dev/shm' not in line:
                continue
            lo, hi = (int(x, 16) for x in line.split()[0].split('-'))
            if hi - lo >= 0x1800000:
                return lo
    except OSError:
        pass
    return None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        print("usage: dolphin-watch.py <disc.iso> [guest address ...]")
        return 2
    iso = sys.argv[1]
    watch = [int(a, 0) for a in sys.argv[2:]] or [0x8021A078]
    seconds = float(os.environ.get('DOLPHIN_WATCH_SECONDS', '240'))

    proc = subprocess.Popen(
        ['flatpak', 'run', '--command=/app/bin/dolphin-emu-nogui',
         '--filesystem=home', 'org.DolphinEmu.dolphin-emu',
         '-p', 'headless', '-v', 'Null', '-e', iso],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    pid = base = None
    for _ in range(90):
        time.sleep(1)
        pid = pid or emulator_pid()
        if pid:
            base = ram_base(pid)
            if base:
                break
    if not base:
        print("could not find Dolphin's RAM mapping")
        proc.kill()
        return 1

    with open('/proc/%d/mem' % pid, 'rb', 0) as mem:
        def read(addr, n=4):
            mem.seek(base + (addr - MEM1))
            return mem.read(n)

        print('emulator pid %d, RAM at 0x%X, game id %r'
              % (pid, base, read(MEM1, 6)))
        sys.stdout.flush()
        last = {}
        start = time.time()
        while time.time() - start < seconds:
            try:
                for addr in watch:
                    value = int.from_bytes(read(addr), 'big')
                    if last.get(addr) != value:
                        print('%7.1fs  0x%08X = 0x%08X'
                              % (time.time() - start, addr, value))
                        last[addr] = value
                sys.stdout.flush()
            except OSError:
                print('emulator went away')
                break
            time.sleep(0.05)
    proc.kill()
    return 0


if __name__ == '__main__':
    sys.exit(main())
