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


def emulator_pid(exclude=frozenset()):
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
                if (fh.read().strip() == 'dolphin-emu-nog'
                        and int(entry) not in exclude):
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
    # An argument may be a single address, or `addr:length` for a RANGE.
    #
    # A range is what answers "which field moves when this works", which is
    # the question a single address cannot: sixteen links were derived by
    # guessing which field to look at next, and each guess cost a run. A
    # struct diff shows them all at once.
    watch, ranges = [], []
    for arg in sys.argv[2:]:
        if ':' in arg:
            base, _, length = arg.partition(':')
            ranges.append((int(base, 0), int(length, 0)))
        else:
            watch.append(int(arg, 0))
    if not watch and not ranges:
        watch = [0x8021A078]
    seconds = float(os.environ.get('DOLPHIN_WATCH_SECONDS', '240'))

    # Whatever is already running is not ours. Attaching to a leftover
    # emulator from an earlier launch reads a process that is about to die,
    # which presents as "emulator went away" after a mapping is found - and
    # cost a ninety-second run to notice.
    already = set()
    while True:
        pid = emulator_pid(already)
        if pid is None:
            break
        already.add(pid)

    proc = subprocess.Popen(
        ['flatpak', 'run', '--command=/app/bin/dolphin-emu-nogui',
         '--filesystem=home', 'org.DolphinEmu.dolphin-emu',
         '-p', 'headless', '-v', 'Null', '-e', iso],
        stdout=open(os.environ.get('DOLPHIN_LOG', '/dev/null'), 'wb'),
        stderr=subprocess.STDOUT)

    pid = base = None
    for _ in range(90):
        time.sleep(1)
        pid = pid or emulator_pid(already)
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
        last, prev = {}, {}
        start = time.time()
        while time.time() - start < seconds:
            try:
                for addr in watch:
                    value = int.from_bytes(read(addr), 'big')
                    if last.get(addr) != value:
                        print('%7.1fs  0x%08X = 0x%08X'
                              % (time.time() - start, addr, value))
                        last[addr] = value
                for base, length in ranges:
                    now = read(base, length)
                    was = prev.get(base)
                    prev[base] = now
                    if was is None or was == now:
                        continue
                    # Report by 4-byte word, which is how these structures are
                    # laid out and how every offset found so far is quoted.
                    for off in range(0, length & ~3, 4):
                        o, n = was[off:off + 4], now[off:off + 4]
                        if o != n:
                            print('%7.1fs  0x%08X +0x%03X  %s -> %s'
                                  % (time.time() - start, base, off,
                                     o.hex(), n.hex()))
                sys.stdout.flush()
            except OSError:
                print('emulator went away')
                break
            time.sleep(0.05)
    proc.kill()
    return 0


if __name__ == '__main__':
    sys.exit(main())
