import struct, sys, os

def u32(b, o): return struct.unpack_from('>I', b, o)[0]

def main(iso_path, out_dir):
    with open(iso_path, 'rb') as f:
        data = f.read()
    assert data[0x1c:0x20] == b'\xc2\x33\x9f\x3d', 'not a GameCube disc'
    game_id = data[0:6].decode('ascii', 'replace')
    disc_no = data[6]
    dol_off = u32(data, 0x420); fst_off = u32(data, 0x424); fst_size = u32(data, 0x428)
    print(f'id={game_id} disc={disc_no} dol=0x{dol_off:x} fst_off=0x{fst_off:x} fst_size={fst_size}')
    os.makedirs(os.path.join(out_dir, 'sys'), exist_ok=True)
    hdr = data[dol_off:dol_off + 0x100]
    dol_size = 0
    for i in range(18):
        off = u32(hdr, i * 4); size = u32(hdr, 0x90 + i * 4)
        if size: dol_size = max(dol_size, off + size)
    open(os.path.join(out_dir, 'sys', 'main.dol'), 'wb').write(data[dol_off:dol_off + dol_size])
    app_total = 0x20 + u32(data, 0x2454) + u32(data, 0x2458)
    open(os.path.join(out_dir, 'sys', 'apploader.img'), 'wb').write(data[0x2440:0x2440 + app_total])
    open(os.path.join(out_dir, 'sys', 'boot.bin'), 'wb').write(data[0:0x440])
    open(os.path.join(out_dir, 'sys', 'bi2.bin'), 'wb').write(data[0x440:0x2440])
    open(os.path.join(out_dir, 'sys', 'fst.bin'), 'wb').write(data[fst_off:fst_off + fst_size])
    fst = data[fst_off:fst_off + fst_size]
    n = u32(fst, 8)
    str_base = 12 * n
    def name(idx):
        no = struct.unpack_from('>I', fst, idx * 12)[0] & 0xffffff
        end = fst.index(b'\x00', str_base + no)
        return fst[str_base + no:end].decode('utf-8', 'replace')
    files_root = os.path.join(out_dir, 'files')
    os.makedirs(files_root, exist_ok=True)
    dir_stack = [(files_root, n)]
    count = 0
    for i in range(1, n):
        typ = fst[i * 12]
        a = u32(fst, i * 12 + 4); b = u32(fst, i * 12 + 8)
        while len(dir_stack) > 1 and i >= dir_stack[-1][1]:
            dir_stack.pop()
        nm = name(i)
        if typ == 1:
            p = os.path.join(dir_stack[-1][0], nm)
            os.makedirs(p, exist_ok=True)
            dir_stack.append((p, b))
        else:
            p = os.path.join(dir_stack[-1][0], nm)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, 'wb').write(data[a:a + b])
            count += 1
    print(f'main.dol={dol_size} apploader={app_total} files={count} -> {out_dir}')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
