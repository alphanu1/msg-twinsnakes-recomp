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
import shlex
import atexit
import signal
import subprocess
import sys
import time

MEM1 = 0x80000000
VMEM = 0x7E000000     # BAT-mapped window; see regions()


# Everything we have started, so cleanup can run from a signal handler as
# well as from the end of main(). `timeout` sends SIGTERM, which otherwise
# kills this script and leaves the emulator behind - which is exactly how
# four of them ended up running at once.
_STARTED = []

def _register(proc, pid):
    _STARTED.append((proc, pid))

def _cleanup_all(*_args):
    while _STARTED:
        proc, pid = _STARTED.pop()
        shutdown(proc, pid)

def _install_handlers():
    atexit.register(_cleanup_all)
    for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        try:
            signal.signal(sig, lambda s, f: (_cleanup_all(), os._exit(128 + s)))
        except (ValueError, OSError):
            pass


def shutdown(proc, pid):
    """Stop the emulator we started, and everything it started.

    TWO WAYS THIS LEAKED, AND BOTH HAPPENED. `proc` is `flatpak run`, and
    the emulator is its GRANDCHILD under bwrap - so killing `proc` left the
    emulator running. And the kill was the last statement of main(), so a
    `timeout` around this script, a Ctrl-C or any exception skipped it
    entirely. Four emulators were left running at once before this was
    noticed, each holding 24 MB of guest RAM and a share of the CPU.

    So: kill the process GROUP (the launcher and bwrap), kill the emulator
    by the pid we already had to find in order to read its memory, and do it
    from a `finally` and from a signal handler rather than from the end of a
    happy path.
    """
    for target in (pid,):
        if not target:
            continue
        try:
            os.kill(target, signal.SIGKILL)
        except OSError:
            pass
    if proc is not None:
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
        except OSError:
            try:
                proc.kill()
            except OSError:
                pass
        try:
            proc.wait(timeout=5)
        except Exception:
            pass


def emulator_pid(exclude=frozenset()):
    """The process holding emulated RAM, found by its MAPPING not its name.

    Matching on `comm` was fragile twice over. The kernel truncates it to 15
    characters, so `pgrep -x dolphin-emu-nogui` matches NOTHING - the name it
    compares against is "dolphin-emu-nog". And the name differs between the
    headless binary and the GUI one, while flatpak adds `dolphin-emu-wra`
    wrappers that own no memory at all. The shared mapping is the thing we
    actually need, so test for that directly: whichever process has a
    /dev/shm/dolphin-emu region of at least MEM1's size IS the emulator.
    """
    me = os.getpid()
    for entry in os.listdir('/proc'):
        if not entry.isdigit() or int(entry) == me or int(entry) in exclude:
            continue
        if ram_base(int(entry)) is not None:
            return int(entry)
    return None


def regions(pid):
    """Guest windows this process backs, as (guest lo, guest hi, host base).

    MEM1 ALONE IS NOT THE GUEST'S MEMORY. This game's loader copies the
    engine overlay to **0x7F008000** - `rel_loader_LoadRel` is called with
    that as its destination - which is not in MEM1 at all. It is a BAT-mapped
    range, and Dolphin backs those with a separate "fake VMEM" mapping at
    file offset 0x02040000 covering 0x7E000000-0x7FFFFFFF.

    Searching only MEM1 therefore finds the loader's TEMPORARY read buffer,
    which by design is never relocated and is freed as soon as the copy is
    made - and concluding from its un-relocated `lis` that the module is
    never linked is exactly the wrong answer that cost this investigation a
    detour. The linked module is in the other window.
    """
    MEM1_OFF, VMEM_OFF = 0x0, 0x02040000
    out = []
    try:
        for line in open('/proc/%d/maps' % pid):
            if 'dolphin-emu' not in line or '/dev/shm' not in line:
                continue
            parts = line.split()
            lo, hi = (int(x, 16) for x in parts[0].split('-'))
            off = int(parts[2], 16)
            if hi - lo < 0x1800000:
                continue
            if off == MEM1_OFF and not any(r[0] == MEM1 for r in out):
                out.append((MEM1, MEM1 + (hi - lo), lo))
            elif off == VMEM_OFF and not any(r[0] == VMEM for r in out):
                out.append((VMEM, VMEM + (hi - lo), lo))
    except OSError:
        pass
    return out


def ram_base(pid):
    """MEM1's host base, or None - the check that this is an emulator."""
    for lo, _hi, host in regions(pid):
        if lo == MEM1:
            return host
    return None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        print("usage: dolphin-watch.py <disc.iso> "
              "[addr | addr:len | =addr | @snapshot-path ...]")
        return 2
    iso = sys.argv[1]
    # An argument may be a single address, or `addr:length` for a RANGE.
    #
    # A range is what answers "which field moves when this works", which is
    # the question a single address cannot: sixteen links were derived by
    # guessing which field to look at next, and each guess cost a run. A
    # struct diff shows them all at once.
    # `=addr` means DUMP that word every second rather than on change.
    # `~addr` WALKS A RECORD RING and reports its tag histogram, the way
    # the engine's own `gcn_pool_acquire` walks it. A backlog of one tag in
    # our run means nothing until the working run is asked whether that is
    # normal, and this is the cheapest way to ask.
    # `@path` writes all of MEM1 to that file at DOLPHIN_DUMP_AT seconds, or
    # when DOLPHIN_DUMP_WHEN's byte string first appears in guest memory.
    # A value that was set before the watcher attached never changes and so
    # is invisible to a change-watch - which is exactly the case for a
    # context pointer written once during boot.
    watch, ranges, polls, dumps, rings = [], [], [], [], []
    for arg in sys.argv[2:]:
        if arg.startswith('='):
            polls.append(int(arg[1:], 0))
        elif arg.startswith('~'):
            rings.append(int(arg[1:], 0))
        elif arg.startswith('@'):
            dumps.append(arg[1:])
        elif ':' in arg:
            base, _, length = arg.partition(':')
            ranges.append((int(base, 0), int(length, 0)))
        else:
            watch.append(int(arg, 0))
    if not watch and not ranges and not polls and not dumps and not rings:
        watch = [0x8021A078]
    seconds = float(os.environ.get('DOLPHIN_WATCH_SECONDS', '240'))
    dump_at = float(os.environ.get('DOLPHIN_DUMP_AT', '60'))
    dump_when = os.environ.get('DOLPHIN_DUMP_WHEN', '').encode() or None
    dump_on = os.environ.get('DOLPHIN_DUMP_ON') or None
    delays = [float(x) for x in
              os.environ.get('DOLPHIN_DUMP_AFTER', '').split(',') if x.strip()]

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

    # DOLPHIN_ARGS passes flags straight through, which is how the
    # emulator's own logging is turned on: it has no --logger option, but
    # -C sets any config key, and Logger.Logs.DVDINTERFACE prints every
    # disc read - the cheapest answer to "when is this file actually read".
    extra = shlex.split(os.environ.get('DOLPHIN_ARGS', ''))
    # DOLPHIN_BIN picks the GUI binary when someone needs to SEE the game;
    # the default stays headless because that is what a batch run wants.
    # An empty DOLPHIN_PLATFORM or DOLPHIN_VIDEO omits the flag entirely,
    # leaving the emulator's own configured backend alone - the GUI build
    # has no "headless" platform, so passing one is worse than passing none.
    binary = os.environ.get('DOLPHIN_BIN', 'dolphin-emu-nogui')
    platform = os.environ.get('DOLPHIN_PLATFORM', 'headless')
    video = os.environ.get('DOLPHIN_VIDEO', 'Null')
    argv = ['flatpak', 'run', '--command=/app/bin/' + binary,
            '--filesystem=home', 'org.DolphinEmu.dolphin-emu']
    if platform:
        argv += ['-p', platform]
    if video:
        argv += ['-v', video]
    proc = subprocess.Popen(
        argv + extra + ['-e', iso],
        stdout=open(os.environ.get('DOLPHIN_LOG', '/dev/null'), 'wb'),
        stderr=subprocess.STDOUT,
        start_new_session=True)   # its own group, so killpg reaches bwrap
    _install_handlers()
    _register(proc, None)

    pid = base = None
    for _ in range(90):
        time.sleep(1)
        pid = pid or emulator_pid(already)
        if pid:
            if _STARTED and _STARTED[-1][1] is None:
                _STARTED[-1] = (proc, pid)
            base = ram_base(pid)
            if base:
                break
    if not base:
        print("could not find Dolphin's RAM mapping")
        shutdown(proc, pid)
        return 1

    windows = regions(pid)
    with open('/proc/%d/mem' % pid, 'rb', 0) as mem:
        def host_for(addr):
            for lo, hi, host in windows:
                if lo <= addr < hi:
                    return host + (addr - lo)
            return None

        def read(addr, n=4):
            at = host_for(addr)
            if at is None:
                return b'\0' * n
            mem.seek(at)
            return mem.read(n)

        print('emulator pid %d, game id %r' % (pid, read(MEM1, 6)))
        for lo, hi, host in windows:
            print('  guest 0x%08X-0x%08X at 0x%X' % (lo, hi, host))
        sys.stdout.flush()
        last, prev = {}, {}
        start = time.time()

        # A SNAPSHOT of all of MEM1, for questions that are not "which word
        # moved" but "where does this structure live". Dolphin's OSLink puts
        # the REL at its own heap address, which is not the one our loader
        # picks, so no address derived on our side can be watched here
        # directly. A snapshot is searched offline for the CODE, whose
        # relocated `lis/addi` immediates give the real guest address of a
        # global - exact, rather than guessed.
        taken, trigger_at = [], 0.0
        for path in dumps:
            # WAITING ON A TIME IS GUESSWORK. The first snapshot was taken at
            # 100s and caught the game before OSLink had even mapped the REL,
            # so every address derived from it would have been of a module
            # that was not there. DOLPHIN_DUMP_WHEN names a byte string that
            # only exists once the interesting code is resident - the REL's
            # own file-name literals serve, being unique and non-relocated -
            # and the snapshot is taken when it appears, however long that is.
            # A pattern trigger fires the instant the BYTES arrive, which
            # for a REL is part way through OSLink: the first snapshot taken
            # this way had its branches relocated and its data references
            # still zero. DOLPHIN_DUMP_AFTER holds off by a given number of
            # seconds per snapshot, so a short series brackets the window
            # between "loaded" and "freed" instead of guessing one instant.
            if taken:
                # Later snapshots in a series are spaced from the moment the
                # trigger fired, not from each other, so the offsets quoted
                # in a finding mean the same thing run to run.
                wait_until = trigger_at + delays[min(len(taken),
                                                     len(delays) - 1)]
                while time.time() - start < wait_until:
                    time.sleep(0.1)
            elif dump_on:
                # A snapshot taken when a PERSON says so. The interesting
                # moment here is one only someone watching the screen can
                # identify, and a time guess has already cost two runs: 100s
                # was after the module had been freed, 12s was before it
                # arrived. Creating the trigger file is the "now".
                while time.time() - start < seconds:
                    if os.path.exists(dump_on):
                        os.unlink(dump_on)
                        break
                    time.sleep(0.25)
            elif dump_when:
                found = False
                while time.time() - start < seconds:
                    mem.seek(base)
                    haystack = mem.read(0x1800000)
                    if dump_when in haystack:
                        found = True
                        break
                    print('%7.1fs  waiting for %r' % (time.time() - start,
                                                      dump_when))
                    sys.stdout.flush()
                    time.sleep(2.0)
                if not found:
                    print('%r never appeared' % dump_when)
                    break
            else:
                while time.time() - start < dump_at:
                    time.sleep(0.2)
            if not taken:
                trigger_at = time.time() - start
                if delays:
                    while time.time() - start < trigger_at + delays[0]:
                        time.sleep(0.1)
            mem.seek(base)          # the snapshot is MEM1; see regions()
            with open(path, 'wb') as out:
                remaining = 0x1800000
                while remaining:
                    chunk = mem.read(min(1 << 20, remaining))
                    if not chunk:
                        break
                    out.write(chunk)
                    remaining -= len(chunk)
            taken.append(path)
            print('%7.1fs  wrote %s (%d bytes)'
                  % (time.time() - start, path, 0x1800000 - remaining))
            sys.stdout.flush()
        while time.time() - start < seconds:
            try:
                for addr in watch:
                    value = int.from_bytes(read(addr), 'big')
                    if last.get(addr) != value:
                        print('%7.1fs  0x%08X = 0x%08X'
                              % (time.time() - start, addr, value))
                        last[addr] = value
                for addr in rings:
                    def w(a):
                        return int.from_bytes(read(a), 'big')
                    base, size = w(addr + 0x08), w(addr + 0x0C)
                    rd, wr = w(addr + 0x14), w(addr + 0x24)
                    key = ('ring', addr)
                    now = time.time() - start
                    if now - last.get(('rt', addr), -9) < 5.0:
                        continue
                    last[('rt', addr)] = now
                    if not base or not size or size > 0x400000 \
                            or not (base <= rd < base + size):
                        print('%7.1fs  ring 0x%08X not ready' % (now, addr))
                        continue
                    tags, p, n, wraps = {}, rd, 0, 0
                    while p != wr and n < 100000:
                        t = w(p)
                        if (t & 0xFF) == 0xFF:
                            p = base
                            wraps += 1
                            if wraps > 4:
                                break
                            continue
                        ln = w(p + 4)
                        # THE WHOLE WORD, not a masked low byte. Some tags
                        # are packed as (language << 16) | id - the consumer
                        # factory at 0x14FCC refuses to create a task unless
                        # (arg >> 16) matches the language byte at
                        # 0x801E7DD8 - so masking hides the one field that
                        # would explain a tag never matching.
                        tags[t] = tags.get(t, 0) + 1
                        n += 1
                        if not ln or ln > size:
                            break
                        p += ln
                        if p >= base + size:
                            p = base
                    used = (wr - rd) % size
                    summary = ' '.join('0x%X:%d' % kv
                                       for kv in sorted(tags.items()))
                    if last.get(key) == summary:
                        continue
                    last[key] = summary
                    print('%7.1fs  ring 0x%08X  used 0x%X/0x%X  %d records  %s'
                          % (now, addr, used, size, n, summary or '(none)'))
                for addr in polls:
                    value = int.from_bytes(read(addr), 'big')
                    now = time.time() - start
                    if last.get(('poll', addr)) != value or now - last.get(
                            ('t', addr), -9) > 5.0:
                        print('%7.1fs  poll 0x%08X = 0x%08X'
                              % (now, addr, value))
                        last[('poll', addr)] = value
                        last[('t', addr)] = now
                for start_addr, length in ranges:
                    now = read(start_addr, length)
                    was = prev.get(start_addr)
                    prev[start_addr] = now
                    if was is None or was == now:
                        continue
                    # Report by 4-byte word, which is how these structures are
                    # laid out and how every offset found so far is quoted.
                    for off in range(0, length & ~3, 4):
                        o, n = was[off:off + 4], now[off:off + 4]
                        if o != n:
                            print('%7.1fs  0x%08X +0x%03X  %s -> %s'
                                  % (time.time() - start, start_addr, off,
                                     o.hex(), n.hex()))
                sys.stdout.flush()
            except OSError:
                print('emulator went away')
                break
            time.sleep(0.05)
    shutdown(proc, pid)
    return 0


if __name__ == '__main__':
    sys.exit(main())
