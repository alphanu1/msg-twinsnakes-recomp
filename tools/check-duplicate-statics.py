#!/usr/bin/env python3
"""Does any file define the same file-scope static VARIABLE twice?

In C, two file-scope `static uint32_t s_x;` in one translation unit are
tentative definitions of ONE object. It is legal, it is silent, and it is
almost never intended - the second declaration quietly aliases the first
rather than creating a new variable.

It cost a boot. `host/module.c` already had `s_watch_addr`, holding OSLink's
address so the host could capture the overlay's .bss base from its arguments;
a new memory watch declared `s_watch_addr` again, set it from the environment
at the top of the run loop, and every run thereafter overwrote OSLink's
address with zero. The overlay's globals were never located and the boot
stopped after loading the module - one file read, one frame drawn - with no
warning from the compiler and nothing in the diff that looked wrong.

`-Wredundant-decls` catches it, but this project deliberately declares a
function immediately before defining it to get external-linkage checking
without a header, and that flag objects to every one of those. So the check
is narrowed to what actually bites: VARIABLES, not functions.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# A file-scope static variable: `static <type...> name` with no '(' before the
# terminator, so a function declaration or definition never matches.
DECL = re.compile(
    r'^static\s+(?!inline\b)[A-Za-z_][\w \t\*]*?([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*[;=,]')


def main():
    bad = []
    for d in ('host', 'runtime'):
        for path in sorted((ROOT / d).rglob('*.c')):
            seen = {}
            for n, line in enumerate(path.read_text(errors='ignore').splitlines(), 1):
                if line[:1].isspace() or '(' in line.split(';')[0]:
                    continue                      # indented => not file scope
                m = DECL.match(line)
                if not m:
                    continue
                name = m.group(1)
                if name in seen:
                    bad.append('  %s: static %s defined at line %d and again '
                               'at line %d' % (path.relative_to(ROOT), name,
                                               seen[name], n))
                else:
                    seen[name] = n
    if bad:
        print('Duplicate file-scope static variables (they are ONE object):')
        print('\n'.join(bad))
        print('\nRename one. See tools/check-duplicate-statics.py for why.')
        return 1
    print('No duplicate file-scope static variables in host/ or runtime/.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
