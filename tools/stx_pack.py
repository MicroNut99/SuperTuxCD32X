#!/usr/bin/env python3
"""stx_pack.py - SuperTux CD32X: the data packs for the disc, from stx_convert.py's output.

    python3 tools/stx_pack.py build/data cd32x/subcpu/ROMS [level indices ...]

Packs are the boot path's OJPK format (port/pack.h, version 2, files only), the same as the
Hurrican and OpenJazz discs, so they go through the unchanged commands:

  IMAGE8.RAW   the resident pack: loaded by the platform layer at start-up (port/plat_mars.c
               pack_load, command 38 slot 8 + command 46) into the RAM cart at 0x100000 and
               mounted as pack 0 before main() runs. Everything that is not one level's:
                 PALETTE.BIN TILES.BIN SPRITES.BIN FONTS.BIN PICS.BIN WMTILES.BIN WORLDS.BIN
                 (stx_convert.py), NAMES.TXT (every level's name and author, for the world
                 maps), INTRO.TXT EXTRO.TXT EXTRO1.TXT EXTRO2.TXT CREDITS.TXT
  OJnnn.PAK    one level each (nnn = level index + 1, LEVELS.TXT; 77 levels: world 1, bonus 1,
               bonus 2, the title's demo level): loaded by the game with plat_load_pack(200 + nnn)
               (command 38 id 200+nnn -> disc file OJnnn.PAK) into the cart at 0x280000 and
               mounted as pack 1.
                 LEVEL.BIN    "STXR", then the layers in YATSSD's form, the rules layer and the
                              objects:
                   0  "STXR"
                   4  w u16, h u16 (tiles), music track u16 (CD track; 0 = none), time u16 (s),
                      gravity u16 (SuperTux's: 10 = normal)
                   14 start x s16, start y s16 (SuperTux's 640x480 units), background u16
                      (0xFFFF = gradient)
                   20 particles u8 (1 snow, 2 clouds), back scrolling u8, objects u16,
                      autoscroll u16 (units a step x 256), background speed u16 (%), pad u32
                   32 layer 0..3, each w*h x u16 (row by row): YATSSD tile numbers -
                      (resource << 2), 0 = nothing; resource r is reslist[r] in game/level.c:
                        1 .. NF        the tile frames of TILES.BIN (frame r - 1)
                        NF+1 .. NF+NB  this level's background tiles (BACKT.BIN tile r - NF - 1)
                      layer 0  the background picture (or gradient) cut into 16x16 tiles - every
                               cell set (YATSSD's base layer)
                      layer 1  SuperTux's background-tm, layer 2 interactive-tm, layer 3
                               foreground-tm (drawn over Tux and the enemies)
                   ..  the rules layer: w*h x u16 SuperTux tile ids of interactive-tm
                   ..  the objects: n x {kind u8, stay-on-platform u8, x s16, y s16}
                   ..  the reset points: n u16, n x {x s16, y s16}
                 BACKT.BIN    64 x u16 colours for palette entries 192-255, then the
                              background tiles, 256 bytes each (16x16, indices 192-255)
"""
import os
import struct
import sys

TILE = 16


def fail(msg):
    sys.exit("*** stx_pack.py: " + msg)


def write_pack(path, files, limit):
    """OJPK version 2 with files only (port/pack.h) - the same writer as Hurrican's hur_convert.py"""
    n = len(files)
    pos = 24 + 24 * n
    offs = []
    for name, data in files:
        pos = (pos + 3) & ~3
        offs.append(pos)
        pos += len(data)
    total = (pos + 3) & ~3
    out = bytearray(b"OJPK" + struct.pack(">IIIII", 2, n, 0, total, 0))
    for (name, data), o in zip(files, offs):
        out += name.encode("ascii").ljust(16, b"\0") + struct.pack(">II", o, len(data))
    for (name, data), o in zip(files, offs):
        out += b"\0" * (o - len(out))
        out += data
    out += b"\0" * (total - len(out))
    if total > limit:
        fail("%s is %d bytes, more than the %d its cart area holds" % (path, total, limit))
    open(path, "wb").write(out)
    return total


def c555(r, g, b):
    return ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)


def rgb_of(c):
    return ((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    data, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    rd = lambda n: open(os.path.join(data, n), "rb").read()
    tiles = rd("TILES.BIN")
    if tiles[:4] != b"STXT":
        fail("TILES.BIN is not stx_convert.py's")
    nframes, ntiles = struct.unpack(">HH", tiles[4:8])
    tdesc = [struct.unpack(">HHHHHH", tiles[8 + 12 * i:20 + 12 * i]) for i in range(ntiles)]
    frame_px = tiles[8 + 12 * ntiles:]
    # SuperTux tile id -> resource. Tile 0 is "nothing" (SuperTux never draws it; its picture is
    # an editor placeholder), and a tile whose first frame is all see-through is nothing too -
    # YATSSD then skips the cell instead of copying 256 transparent pixels
    res_of = []
    for t, d in enumerate(tdesc):
        empty = not any(frame_px[TILE * TILE * d[0]:TILE * TILE * (d[0] + 1)]) if d[1] else True
        res_of.append(0 if t == 0 or empty else d[0] + 1)

    # ---- the resident pack
    res_files = ["PALETTE.BIN", "TILES.BIN", "SPRITES.BIN", "FONTS.BIN", "PICS.BIN", "WMTILES.BIN", "WORLDS.BIN",
                 "NAMES.TXT", "INTRO.TXT", "EXTRO.TXT", "EXTRO1.TXT", "EXTRO2.TXT", "CREDITS.TXT"]
    size = write_pack(os.path.join(out, "IMAGE8.RAW"), [(n, rd(n)) for n in res_files], 0x180000)
    print("  IMAGE8.RAW: %d bytes (%s)" % (size, ", ".join(res_files)))

    backs = rd("BACKS.BIN")
    nb = struct.unpack(">H", backs[4:6])[0]
    back = []
    for i in range(nb):
        e = backs[6 + 24 * i:30 + 24 * i]
        w, h, off = struct.unpack(">HHI", e[16:24])
        back.append((backs[off:off + w * h], backs[off + w * h:off + w * h + 128], w, h))

    nlevels = len([l for l in open(os.path.join(data, "LEVELS.TXT")) if l.strip()])
    levels = [int(a) for a in sys.argv[3:]] or list(range(nlevels))
    total = 0
    for li in levels:
        lv = rd("L%03d.BIN" % li)
        (W, H, bgi, top, bot, track, ltime, grav, part, bscroll, sx, sy, nobj, autos, bspeed) = \
            struct.unpack(">HHHHHHHHBBhhHHH", lv[4:32])
        base = 32
        cells = W * H
        lay = [struct.unpack(">%dH" % cells, lv[base + 2 * cells * k:base + 2 * cells * (k + 1)]) for k in range(3)]
        objs = lv[base + 6 * cells:base + 6 * cells + 6 * nobj]
        o = base + 6 * cells + 6 * nobj
        nres = struct.unpack(">H", lv[o:o + 2])[0]
        resets = lv[o:o + 2 + 4 * nres]
        if bgi != 0xFFFF:
            px, cols, bw, bh = back[bgi]
            ncol, nrow = bw // TILE, bh // TILE
            bt = bytearray()
            for ty in range(nrow):
                for tx in range(ncol):
                    for y in range(TILE):
                        o = (ty * TILE + y) * bw + tx * TILE
                        bt += px[o:o + TILE]
            colours = cols
            l0 = [nframes + 1 + (ty % nrow) * ncol + (tx % ncol) for ty in range(H) for tx in range(W)]
        else:
            # SuperTux's two-colour gradient over the level's height: 64 shades (192-255), a tile a row
            t, b = rgb_of(top), rgb_of(bot)
            colours = b"".join(struct.pack(">H", c555(*[int(t[c] + (b[c] - t[c]) * s / 63) for c in range(3)]) or 0x0400)
                               for s in range(64))
            bt = bytearray()
            for ty in range(H):
                for y in range(TILE):
                    shade = min(63, (ty * TILE + y) * 64 // (H * TILE))
                    bt += bytes([192 + shade]) * TILE
            l0 = [nframes + 1 + ty for ty in range(H) for tx in range(W)]
        if nframes + 1 + len(bt) // (TILE * TILE) >= 8192:
            fail("too many resources for YATSSD's 16-bit tile numbers")

        def yl(ids):
            return [(res_of[t] if t < ntiles else 0) << 2 for t in ids]
        body = b"STXR" + struct.pack(">HHHHHhhHBBHHHI", W, H, track, ltime, grav, sx, sy, bgi, part, bscroll,
                                     nobj, autos, bspeed, 0)
        assert len(body) == 32
        for L in ([r << 2 for r in l0], yl(lay[0]), yl(lay[1]), yl(lay[2])):
            body += struct.pack(">%dH" % cells, *L)
        body += struct.pack(">%dH" % cells, *[t if t < ntiles else 0 for t in lay[1]])
        body += objs + resets
        name = "OJ%03d.PAK" % (li + 1)
        size = write_pack(os.path.join(out, name), [("LEVEL.BIN", body), ("BACKT.BIN", bytes(colours) + bytes(bt))],
                          0x170000)
        total += size
    print("  OJ%03d.PAK - OJ%03d.PAK: %d levels, %d bytes" % (levels[0] + 1, levels[-1] + 1, len(levels), total))


if __name__ == "__main__":
    main()
