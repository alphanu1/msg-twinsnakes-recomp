#!/usr/bin/env python3
"""Generate module_tables.inc covering BOTH main.dol and mgso_pal.rel.

ModernGekko's own gen_module_tables.py reads one DOL. Twin Snakes needs two
modules in one descriptor, because 92% of its code is in the REL, so this
does the same job over both and merges the tables.

The tables tell the runtime three things:

  code_ranges   which guest addresses this module has native code for
  smc_ranges    which addresses may be patched at runtime
  chunk_ranges  + chunk_hashes: FNV-1a-64 of the ORIGINAL bytes, so the
                runtime can check guest RAM still holds the code we compiled
                from, and fall back to the interpreter when it does not

The hashes are why this cannot just concatenate two .inc files: each range's
bytes have to be read out of the file that range came from.
"""
import re, sys, argparse
from pathlib import Path

FNV64_OFFSET, FNV64_PRIME, MASK64 = 0xCBF29CE484222325, 0x100000001B3, (1 << 64) - 1

def fnv1a64(data: bytes) -> int:
    h = FNV64_OFFSET
    for b in data:
        h = ((h ^ b) * FNV64_PRIME) & MASK64
    return h

def dol_reader(path: Path):
    """Guest address -> bytes, via the DOL's 18 section descriptors."""
    dol = path.read_bytes()
    be32 = lambda o: int.from_bytes(dol[o:o + 4], "big")
    sections = []
    for i in range(18):
        off, addr, size = be32(i * 4), be32(0x48 + i * 4), be32(0x90 + i * 4)
        if off and addr and size:
            sections.append((addr, size, off))
    def read(start, end):
        for addr, size, off in sections:
            if addr <= start and end <= addr + size:
                lo = off + (start - addr)
                return dol[lo:lo + (end - start)]
        raise ValueError(f"[0x{start:08X},0x{end:08X}) is not inside one DOL section")
    return read

def rel_reader(path: Path, base: int):
    """Guest address -> bytes for a REL loaded at `base`.

    DolRecomp lays a REL out at REL_AUTO_BASE with file offsets preserved, so
    a guest address maps to the file by simple subtraction. Verified against
    the entry point: .text sits at file offset 0xEC and DolRecomp reports the
    entry at base+0xEC.
    """
    rel = path.read_bytes()
    def read(start, end):
        lo, hi = start - base, end - base
        if lo < 0 or hi > len(rel):
            raise ValueError(f"[0x{start:08X},0x{end:08X}) is outside the REL")
        return rel[lo:hi]
    return read

def parse_ranges(header: str):
    """Coverage ranges, in both forms DolRecomp emits."""
    out = {(int(a, 16), int(b, 16)) for a, b in re.findall(
        r"address >= (0x[0-9A-Fa-f]+)u && address < (0x[0-9A-Fa-f]+)u", header)}
    for base, span in re.findall(
            r"u32\s+offset\s*=\s*address\s*-\s*(0x[0-9A-Fa-f]+)u\s*;\s*"
            r"if\s*\(\s*offset\s*<\s*(0x[0-9A-Fa-f]+)u", header):
        s = int(base, 16)
        out.add((s, s + int(span, 16)))
    return out

def parse_smc(text: str):
    """generated_smc.txt lines are `0xSTART-0xEND`, with END an inclusive
    instruction address. The ABI wants end-exclusive byte ranges."""
    out = []
    for line in text.splitlines():
        m = re.match(r"(0x[0-9A-Fa-f]+)-(0x[0-9A-Fa-f]+)", line.strip())
        if m:
            out.append((int(m.group(1), 16), int(m.group(2), 16) + 4))
    return sorted(out)

def parse_chunks(header: str, code):
    """One range per generated func_XXXXXXXX translation unit.

    A chunk is the SMC demotion granule: a patch anywhere inside one retires
    that whole chunk to the interpreter. So chunk boundaries are not
    cosmetic - they decide how much native code a single patched instruction
    costs us. Each chunk runs to the next func_ in the same code range.
    """
    funcs = sorted(int(a, 16) for a in
                   re.findall(r"void func_([0-9A-Fa-f]{8})\(CPUState\* ctx\);", header))
    if not funcs:
        raise SystemExit("no func_ declarations found - wrong generated.h?")
    out = []
    for i, addr in enumerate(funcs):
        holder = next(((a, b) for a, b in code if a <= addr < b), None)
        if holder is None:
            raise SystemExit(f"func_{addr:08X} is outside every code range")
        end = holder[1]
        if i + 1 < len(funcs) and holder[0] <= funcs[i + 1] < holder[1]:
            end = funcs[i + 1]
        out.append((addr, end))
    return out

def collect(generated: Path, reader):
    header = (generated / "generated.h").read_text()
    smc_file = generated / "generated_smc.txt"
    smc = parse_smc(smc_file.read_text()) if smc_file.exists() else []
    code = sorted(parse_ranges(header))
    chunks = parse_chunks(header, code)
    return code, smc, [(a, b, fnv1a64(reader(a, b))) for a, b in chunks]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dol-generated', required=True)
    ap.add_argument('--dol', required=True)
    ap.add_argument('--rel-generated', required=True)
    ap.add_argument('--rel', required=True)
    # The address the GAME loads its overlay at, not one we pick. See the
    # comment on REL_BASE in CMakeLists.txt: this must match the --rel-base
    # the overlay was recompiled with, or every data address in the generated
    # code points somewhere the data is not.
    ap.add_argument('--rel-base', default='0x7F008000')
    ap.add_argument('--out', required=True)
    # REL metadata, from `dtk rel info`. Declared rather than re-parsed so the
    # values in the module are the ones a human checked.
    ap.add_argument('--rel-module-id', type=int, default=1)
    ap.add_argument('--rel-version', type=int, default=3)
    ap.add_argument('--rel-section-count', type=int, default=20)
    ap.add_argument('--rel-text-index', type=int, default=1)
    ap.add_argument('--rel-text-off', type=lambda x: int(x, 0), default=0xEC)
    ap.add_argument('--rel-text-size', type=lambda x: int(x, 0), default=0x456400)
    a = ap.parse_args()

    # Read the REL header rather than trusting flags. The runtime validates
    # section_info_offset >= 0x40 and file_size >= 0x40, and a hand-passed 0
    # for the former is rejected as "invalid REL module metadata" - which
    # names the struct but not the field.
    hdr = Path(a.rel).read_bytes()[:0x50]
    be = lambda o: int.from_bytes(hdr[o:o + 4], 'big')
    a.rel_module_id = be(0x00)
    a.rel_section_count = be(0x0C)
    a.rel_section_info_offset = be(0x10)
    a.rel_version = be(0x1C)
    a.rel_file_size = Path(a.rel).stat().st_size
    print(f"REL header: id={a.rel_module_id} version={a.rel_version} "
          f"sections={a.rel_section_count} "
          f"section_info_offset=0x{a.rel_section_info_offset:X} "
          f"file_size=0x{a.rel_file_size:X}")

    dol_code, dol_smc, dol_chunks = collect(Path(a.dol_generated), dol_reader(Path(a.dol)))
    rel_code, rel_smc, rel_chunks = collect(
        Path(a.rel_generated), rel_reader(Path(a.rel), int(a.rel_base, 16)))

    code = sorted(set(dol_code) | set(rel_code))
    smc = sorted(set(dol_smc) | set(rel_smc))
    chunks = sorted(dol_chunks + rel_chunks)

    with open(a.out, 'w') as f:
        f.write("// Generated by game/module/gen_combined_tables.py - do not edit.\n")
        f.write("// Covers main.dol AND mgso_pal.rel.\n")
        f.write("static const StaticRecompRange s_code_ranges[] = {\n")
        for x, y in code: f.write(f"    {{0x{x:08X}u, 0x{y:08X}u}},\n")
        f.write(f"}};\n#define MODULE_CODE_RANGE_COUNT {len(code)}u\n")
        f.write("static const StaticRecompRange s_smc_ranges[] = {\n")
        for x, y in smc: f.write(f"    {{0x{x:08X}u, 0x{y:08X}u}},\n")
        if not smc: f.write("    {0u, 0u}, /* C and MSVC reject zero-sized arrays. */\n")
        f.write(f"}};\n#define MODULE_SMC_RANGE_COUNT {len(smc)}u\n")
        f.write("static const StaticRecompRange s_chunk_ranges[] = {\n")
        for x, y, _ in chunks: f.write(f"    {{0x{x:08X}u, 0x{y:08X}u}},\n")
        f.write(f"}};\n#define MODULE_CHUNK_RANGE_COUNT {len(chunks)}u\n")
        f.write("static const u64 s_chunk_hashes[] = {\n")
        for _, _, h in chunks: f.write(f"    0x{h:016X}u,\n")
        f.write("};\n")

        # REL section table. ONLY executable sections are declared: the
        # runtime validates that every section's linked_start falls inside a
        # code range, so declaring .rodata or .data - which have no recompiled
        # code and therefore no code range - would fail validation outright.
        f.write("static const ModernGekkoRelSection s_rel_sections[] = {\n")
        f.write(f"    {{{a.rel_module_id}u, {a.rel_text_index}u, "
                f"0x{int(a.rel_base,16) + a.rel_text_off:08X}u, "
                f"0x{a.rel_text_size:08X}u}},\n")
        f.write("};\n")
        f.write("static const ModernGekkoRelModule s_rel_modules[] = {\n")
        f.write(f"    {{{a.rel_module_id}u, {a.rel_version}u, "
                f"{a.rel_section_count}u, 0x{a.rel_section_info_offset:X}u, "
                f"0x{a.rel_file_size:08X}u, "
                f"s_rel_sections, 1u}},\n")
        f.write("};\n")
        f.write("#define MODULE_REL_MODULE_COUNT 1u\n")

    print(f"{a.out}: {len(code)} code ranges ({len(dol_code)} DOL + {len(rel_code)} REL), "
          f"{len(smc)} smc ranges, {len(chunks)} chunk ranges (hashed)")

if __name__ == '__main__':
    sys.exit(main())
