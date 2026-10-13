#!/usr/bin/env python3
"""stx_convert.py - SuperTux 0.1.3 ("Milestone 1") for the CD32X: tiles, palette, levels, previews.

    python3 tools/stx_convert.py <SuperTux 0.1.3 folder> <out folder> [--previews <folder>]

<SuperTux 0.1.3 folder> is the source tree of tag v0.1.3 (tools/stx_fetch.sh gets it); its data/
folder is read. Nothing of SuperTux is stored in this repository.

Everything is halved (640x480 -> 320x240: 32x32 tiles -> 16x16) and turned into 256 colours:

    0          transparent in tiles and sprites; the backdrop on screen
    1-191      the global colours: every tile and every sprite (SuperTux 0.1.3 has one tile set,
               shared by all levels, so one palette serves the whole game)
    192-255    the level's background: its picture (64 colours) or its two-colour gradient
               (64 shades) - exchanged per level

Soft edges: SuperTux's PNGs have anti-aliased alpha; the 32X draws keyed, so after halving
(premultiplied, Lanczos) a pixel is visible when its alpha is at least 50 %.

Files written (big-endian; offsets from the start of the file). Provisional - R1 measures, the
first engine build (R1b) settles them:
  PALETTE.BIN  192 x u16 32X colours (0BBBBBGGGGGRRRRR), entries 0-191 (0 unused)
  TILES.BIN    "STXT" nframes u16, ntiles u16;
               ntiles x {first frame u16, frames u16, anim speed ms u16, flags u16}
                 flags: 1 solid, 2 ice, 4 water, 8 brick, 16 fullbox (bonus block), 32 distro
                 (coin block), 64 coin, 128 goal, 256 spike, 512 unisolid (jump through)
               nframes x 16x16 pixels
  BACKS.BIN    "STXB" n u16; n x {name[16], w u16, h u16, offset u32}; pictures 320x240,
               64 colours each (192-255) + their 64 palette entries (u16)
  LEVEL.BIN    one per level, Lnnn.BIN (nnn = index in LEVELS.TXT):
               "STXL" w u16 h u16, background u16 (0xFFFF: gradient), gradient top u16 bottom u16
               (32X colours), music track u16 (CD track; +1 = its fast version), time u16, gravity u16, particles u8, pad u8,
               start x s16 y s16 (SuperTux's 640x480 units), nobjects u16, pad u16;
               3 layers (background, interactive, foreground) x w*h tile numbers (u16; the
               tile number is SuperTux's tile id - ids above 255 occur), objects nobjects x {kind u8, pad u8, x s16, y s16}
  SPRITES.BIN  "STXS" nsprites u16, nframes u16; nsprites x {name[24], frames u16, fps x 10 u16,
               x-hotspot s16, y-hotspot s16 (SuperTux's 640x480 units), first frame u16, pad u16};
               nframes x {w u16 (a multiple of 8), h u16, offset u32}; pixels (4-byte aligned)
  LEVELS.TXT   index, set, file, name, size
  REPORT.TXT   the measurements
"""
import os
import re
import struct
import sys
import zlib

import numpy as np
from PIL import Image

HALF = 2
TILE = 32 // HALF
GLOBAL_COLOURS = 191       # 1..191
BACK_FIRST = 192
BACK_COLOURS = 64          # 192..255
SCREEN_W, SCREEN_H = 320, 224
OBJ_KINDS = ["snowball", "bouncingsnowball", "flyingsnowball", "mriceblock", "mrbomb", "jumpy",
             "spiky", "stalactite", "fish", "money", "flame", "point", "bsod", "laptop"]
FLAG_NAMES = [("solid", 1), ("ice", 2), ("water", 4), ("brick", 8), ("fullbox", 16), ("distro", 32),
              ("coin", 64), ("goal", 128), ("spike", 256), ("unisolid", 512)]
# CD audio tracks (1 is the data track); tools/stx_music.sh renders them in this order.
# A level's track is its song; the song's "-fast" version (SuperTux plays it when the time runs
# low) is always the next track.
MUSIC = ["theme", "Mortimers_chipdisko", "Mortimers_chipdisko-fast", "cave", "cave-fast",
         "fortress", "fortress-fast", "fortress2", "fortress2-fast", "SALCON", "SALCON-fast",
         "leveldone"]
MUSIC_TRACK = {m.lower(): 2 + i for i, m in enumerate(MUSIC)}


def sexp(text):
    text = re.sub(r";[^\n]*", "", text)
    toks = re.findall(r'\(|\)|"[^"]*"|[^\s()]+', text)
    out, stack = [], []
    cur = out
    for t in toks:
        if t == "(":
            stack.append(cur)
            cur.append([])
            cur = cur[-1]
        elif t == ")":
            cur = stack.pop()
        else:
            cur.append(t[1:-1] if t.startswith('"') else t)
    return out


def find(lst, key):
    for e in lst:
        if isinstance(e, list) and e and e[0] == key:
            return e
    return None


def val(lst, key, default=None):
    e = find(lst, key)
    return e[1] if e and len(e) > 1 else default


def num(lst, key, default=0):
    """a number from the level file (some levels write 10.0 where others write 10)"""
    return int(round(float(val(lst, key, default))))


def c555(r, g, b):
    return ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)


def halve(im):
    """RGBA picture -> half size, alpha cut at 50 % (premultiplied resize: no dark fringes)."""
    im = im.convert("RGBA")
    w, h = max(1, im.width // HALF), max(1, im.height // HALF)
    pm = im.convert("RGBa").resize((w, h), Image.LANCZOS).convert("RGBA")
    a = np.array(pm)
    a[..., 3] = np.where(a[..., 3] >= 128, 255, 0)
    return a


class Palette:
    def __init__(self, samples):
        """samples: list of RGBA arrays; builds GLOBAL_COLOURS from their opaque pixels."""
        px = np.concatenate([s[s[..., 3] == 255][:, :3] for s in samples if (s[..., 3] == 255).any()])
        img = Image.fromarray(px.reshape(1, -1, 3).astype(np.uint8), "RGB")
        q = img.quantize(colors=GLOBAL_COLOURS, method=Image.MEDIANCUT, dither=Image.NONE)
        pal = np.array(q.getpalette()[:GLOBAL_COLOURS * 3], dtype=np.int32).reshape(-1, 3)
        self.rgb = np.zeros((256, 3), dtype=np.int32)
        self.rgb[1:1 + len(pal)] = pal
        self.count = len(pal)

    def index(self, a):
        """RGBA array -> indexed (0 = transparent), nearest global colour."""
        rgb = a[..., :3].astype(np.int32).reshape(-1, 3)
        pal = self.rgb[1:1 + self.count]
        out = np.zeros(len(rgb), dtype=np.uint8)
        for s in range(0, len(rgb), 4096):
            d = ((rgb[s:s + 4096, None, :] - pal[None, :, :]) ** 2).sum(-1)
            out[s:s + 4096] = d.argmin(1) + 1
        out[a[..., 3].reshape(-1) == 0] = 0
        return out.reshape(a.shape[:2])


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    src, outdir = sys.argv[1], sys.argv[2]
    prevdir = sys.argv[sys.argv.index("--previews") + 1] if "--previews" in sys.argv else None
    D = os.path.join(src, "data")
    if not os.path.exists(os.path.join(D, "images/tilesets/supertux.stgt")):
        sys.exit("*** no SuperTux 0.1.3 data in %s (tools/stx_fetch.sh gets it)" % src)
    os.makedirs(outdir, exist_ok=True)
    rep = []

    # ---- tile set ----
    ts = sexp(open(os.path.join(D, "images/tilesets/supertux.stgt")).read())[0]
    tiles = {}
    for e in ts[1:]:
        if isinstance(e, list) and e and e[0] == "tile":
            tid = int(val(e, "id"))
            ims = find(e, "images")
            flags = 0
            for name, bit in FLAG_NAMES:
                if val(e, name) == "#t":
                    flags |= bit
            tiles[tid] = {"images": ims[1:] if ims else [], "flags": flags,
                          "speed": int(val(e, "anim-speed", 25))}
    frames, frame_of = [], {}
    raw_tiles = {}
    for tid in sorted(tiles):
        arrs = []
        for nm in tiles[tid]["images"]:
            p = os.path.join(D, "images/tilesets", nm)
            if os.path.exists(p):
                arrs.append(halve(Image.open(p)))
        raw_tiles[tid] = arrs

    # ---- sprites (for the palette now; packed in R1b) ----
    sprite_arrs = []
    for d in ("shared", "status"):
        for nm in sorted(os.listdir(os.path.join(D, "images", d))):
            if nm.lower().endswith(".png"):
                im = Image.open(os.path.join(D, "images", d, nm))
                if d == "status" and im.width * im.height > 120 * 120:
                    continue     # menu panels: drawn by the port's own menus
                sprite_arrs.append(halve(im))

    pal = Palette([a for v in raw_tiles.values() for a in v] + sprite_arrs)
    ntiles = max(tiles) + 1
    tile_px = []
    tdesc = []
    for tid in range(ntiles):
        t = tiles.get(tid)
        arrs = raw_tiles.get(tid, [])
        first = len(tile_px)
        for a in arrs:
            fr = np.zeros((TILE, TILE), dtype=np.uint8)
            ia = pal.index(a)
            fr[:ia.shape[0], :ia.shape[1]] = ia[:TILE, :TILE]
            tile_px.append(fr)
        tdesc.append((first, len(arrs), t["speed"] if t else 0, t["flags"] if t else 0))
    with open(os.path.join(outdir, "TILES.BIN"), "wb") as f:
        f.write(b"STXT" + struct.pack(">HH", len(tile_px), ntiles))
        for d in tdesc:
            f.write(struct.pack(">HHHH", *d))
        for fr in tile_px:
            f.write(fr.tobytes())
    with open(os.path.join(outdir, "PALETTE.BIN"), "wb") as f:
        for r, g, b in pal.rgb[:BACK_FIRST]:
            f.write(struct.pack(">H", c555(int(r), int(g), int(b))))
    write_sprites(D, outdir, pal)
    spr_bytes = sum(int((a[..., 3] == 255).sum()) for a in sprite_arrs)
    rep.append("tiles: %d ids, %d frames of 16x16 = %d bytes; global colours used: %d"
               % (ntiles, len(tile_px), len(tile_px) * TILE * TILE, pal.count))
    rep.append("sprites (shared + small status pictures): %d, %d visible pixels after halving"
               % (len(sprite_arrs), spr_bytes))

    # ---- backgrounds ----
    backs, back_index = [], {}
    for nm in sorted(os.listdir(os.path.join(D, "images/background"))):
        im = Image.open(os.path.join(D, "images/background", nm)).convert("RGB").resize((320, 240), Image.LANCZOS)
        q = im.quantize(colors=BACK_COLOURS, method=Image.MEDIANCUT, dither=Image.NONE)
        px = (np.array(q, dtype=np.uint16) + BACK_FIRST).astype(np.uint8)
        bp = np.array(q.getpalette()[:BACK_COLOURS * 3]).reshape(-1, 3)
        back_index[nm] = len(backs)
        backs.append((nm, px, bp))
    with open(os.path.join(outdir, "BACKS.BIN"), "wb") as f:
        hdr = 6 + len(backs) * 24
        f.write(b"STXB" + struct.pack(">H", len(backs)))
        off = hdr
        for nm, px, bp in backs:
            f.write(nm.encode()[:16].ljust(16, b"\0") + struct.pack(">HHI", 320, 240, off))
            off += px.size + BACK_COLOURS * 2
        for nm, px, bp in backs:
            f.write(px.tobytes())
            for r, g, b in bp:
                f.write(struct.pack(">H", c555(int(r), int(g), int(b))))
    rep.append("backgrounds: %d pictures, 320x240 in 64 colours = %d bytes each"
               % (len(backs), 320 * 240))

    # ---- levels ----
    lvdir = os.path.join(D, "levels")
    sets = ["world1", "bonus1", "bonus2", "misc"]
    files = []
    for s in sets:
        p = os.path.join(lvdir, s)
        if os.path.isdir(p):
            names = sorted((n for n in os.listdir(p) if n.endswith(".stl")),
                           key=lambda n: [int(x) if x.isdigit() else x for x in re.split(r"(\d+)", n)])
            files += [(s, n) for n in names]
    listing, total_map, total_z = [], 0, 0
    kinds_seen = {}
    music_used = {}
    tallest = 0
    previews = []
    for i, (s, n) in enumerate(files):
        lv = sexp(open(os.path.join(lvdir, s, n), errors="replace").read())[0]
        W, H = num(lv, "width", 0), num(lv, "height", 15)   # SuperTux: 15 when not given (menu.stl)
        tallest = max(tallest, H)
        layers = []
        for nm in ("background-tm", "interactive-tm", "foreground-tm"):
            e = find(lv, nm)
            v = [int(x) for x in e[1:]] if e else []
            v = (v + [0] * (W * H))[:W * H]
            layers.append(np.array(v, dtype=np.uint16).reshape(H, W))
        bg = val(lv, "background", "")
        bgi = back_index.get(bg, 0xFFFF) if bg else 0xFFFF
        top = c555(num(lv, "bkgd_red_top"), num(lv, "bkgd_green_top"), num(lv, "bkgd_blue_top"))
        bot = c555(num(lv, "bkgd_red_bottom"), num(lv, "bkgd_green_bottom"), num(lv, "bkgd_blue_bottom"))
        mus = val(lv, "music", "")
        track = MUSIC_TRACK.get(os.path.splitext(mus)[0].lower(), 0)
        music_used[mus] = track
        objs = []
        oe = find(lv, "objects")
        for o in (oe[1:] if oe else []):
            if isinstance(o, list) and o:
                k = o[0]
                kinds_seen[k] = kinds_seen.get(k, 0) + 1
                kid = OBJ_KINDS.index(k) if k in OBJ_KINDS else 255
                objs.append((kid, num(o, "x"), num(o, "y")))
        sx = num(lv, "start_pos_x", 100)
        sy = num(lv, "start_pos_y", 100)
        part = {"": 0, "snow": 1, "clouds": 2}.get(val(lv, "particle_system", ""), 0)
        body = b"STXL" + struct.pack(">HHHHHHHHBBhhHH", W, H, bgi, top, bot, track,
                                     num(lv, "time"), num(lv, "gravity", 10), part, 0,
                                     sx, sy, len(objs), 0)
        body += b"".join(l.astype(">u2").tobytes() for l in layers)
        body += b"".join(struct.pack(">BBhh", k, 0, x, y) for k, x, y in objs)
        with open(os.path.join(outdir, "L%03d.BIN" % i), "wb") as f:
            f.write(body)
        z = len(zlib.compress(body, 9))
        total_map += len(body)
        total_z += z
        name = val(lv, "name", n)
        listing.append("%3d  %-7s %-24s %-40s %4dx%-3d %7d bytes (%6d packed)"
                       % (i, s, n, name[:40], W, H, len(body), z))
        if prevdir:
            previews.append((i, s, n, name, render(layers, tdesc, tile_px, pal, backs, bgi, top, bot, W, H, objs)))
    with open(os.path.join(outdir, "LEVELS.TXT"), "w") as f:
        f.write("\n".join(listing) + "\n")
    rep.append("levels: %d, tallest %d tiles (%d lines at half size; the screen shows %d)"
               % (len(files), tallest, tallest * TILE, SCREEN_H))
    rep.append("levels: %d bytes as stored, %d bytes compressed (zlib, as a measure)" % (total_map, total_z))
    rep.append("music used by levels: " + ", ".join("%s -> track %d" % (m or "(none)", t)
               for m, t in sorted(music_used.items(), key=lambda x: x[1])))
    rep.append("objects: " + ", ".join("%s %d" % kv for kv in sorted(kinds_seen.items(), key=lambda x: -x[1])))
    unknown = sorted(k for k in kinds_seen if k not in OBJ_KINDS)
    if unknown:
        rep.append("objects without a kind number yet: " + ", ".join(unknown))
    with open(os.path.join(outdir, "REPORT.TXT"), "w") as f:
        f.write("\n".join(rep) + "\n")
    print("\n".join(rep))

    if prevdir:
        os.makedirs(prevdir, exist_ok=True)
        thumbs = []
        for i, s, n, name, img in previews:
            img.save(os.path.join(prevdir, "L%03d_%s_%s.png" % (i, s, n[:-4])))
            thumbs.append(img.crop((0, 0, SCREEN_W, SCREEN_H)).resize((SCREEN_W // 2, SCREEN_H // 2), Image.NEAREST))
        cols = 7
        rows = (len(thumbs) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * 164, rows * 116), (32, 32, 32))
        for k, t in enumerate(thumbs):
            sheet.paste(t, ((k % cols) * 164 + 2, (k // cols) * 116 + 2))
        sheet.save(os.path.join(prevdir, "ALL_LEVELS_START.png"))
        print("previews: %d levels -> %s" % (len(previews), prevdir))


def write_sprites(D, outdir, pal):
    """SPRITES.BIN: the sprites the game draws (SPRITES below), from SuperTux's supertux.strf
    (name, fps, hotspot, pictures), halved and in the global colours (0 = transparent).
    Every picture is padded on the right to a width that is a multiple of 8 and starts on a
    4-byte boundary: YATSSD's 16-bit copy loop (draw_inc.h PIX_LOOP2) then copies 32 bits at
    a time, and its loops never get fewer than 4 units (its counter would wrap - Sonic32 T7)."""
    res = sexp(open(os.path.join(D, "supertux.strf")).read())[0]
    defs = {}
    for e in res[1:]:
        if isinstance(e, list) and e and e[0] == "sprite":
            ims = find(e, "images")
            defs[val(e, "name")] = (float(val(e, "fps", 10)), num(e, "x-hotspot"), num(e, "y-hotspot"),
                                   ims[1:] if ims else [])
    entries, frames, pix = [], [], []
    for name in SPRITES:
        fps, hx, hy, ims = defs[name]
        first = len(frames)
        for im in ims:
            a = halve(Image.open(os.path.join(D, "images", im)))
            idx = pal.index(a)
            h, w = idx.shape
            pw = (w + 7) & ~7
            p = np.zeros((h, pw), dtype=np.uint8)
            p[:, :w] = idx
            frames.append((pw, h))
            pix.append(p.tobytes())
        entries.append((name, len(ims), int(round(fps * 10)), hx, hy, first))
    head = 8 + 36 * len(entries) + 8 * len(frames)
    out = bytearray(b"STXS" + struct.pack(">HH", len(entries), len(frames)))
    for name, n, fps10, hx, hy, first in entries:
        out += name.encode()[:24].ljust(24, b"\0") + struct.pack(">HHhhHH", n, fps10, hx, hy, first, 0)
    off = (head + 3) & ~3
    offs = []
    for p in pix:
        offs.append(off)
        off = (off + len(p) + 3) & ~3
    for (w, h), o in zip(frames, offs):
        out += struct.pack(">HHI", w, h, o)
    for p, o in zip(pix, offs):
        out += b"\0" * (o - len(out)) + p
    out += b"\0" * ((-len(out)) & 3)
    with open(os.path.join(outdir, "SPRITES.BIN"), "wb") as f:
        f.write(out)


# the sprites the game draws (R1b: small Tux); SPRITES.BIN keeps this order (game/stx.c SPR_*)
SPRITES = ["smalltux-stand-left", "smalltux-stand-right", "smalltux-walk-left", "smalltux-walk-right",
           "smalltux-jump-left", "smalltux-jump-right", "smalltux-skid-left", "smalltux-skid-right"]


def render(layers, tdesc, tile_px, pal, backs, bgi, top, bot, W, H, objs):
    """The whole level as the 32X would draw it (first animation frame of every tile), with the
    view's 224 lines taken from the bottom of the level and the start objects marked."""
    full = np.zeros((H * TILE, W * TILE), dtype=np.uint8)
    rgb = pal.rgb.copy()
    if bgi != 0xFFFF:
        nm, px, bp = backs[bgi]
        rgb[BACK_FIRST:BACK_FIRST + BACK_COLOURS] = bp
        for x0 in range(0, W * TILE, 320):            # SuperTux scrolls its picture at half speed;
            seg = px[:min(240, H * TILE), :min(320, W * TILE - x0)]   # here: tiled, for the look
            full[:seg.shape[0], x0:x0 + seg.shape[1]] = seg
    else:
        def unpack(c):
            return np.array([(c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3])
        t, b = unpack(top), unpack(bot)
        for k in range(BACK_COLOURS):
            rgb[BACK_FIRST + k] = t + (b - t) * k // (BACK_COLOURS - 1)
        for y in range(H * TILE):
            full[y, :] = BACK_FIRST + y * (BACK_COLOURS - 1) // max(1, H * TILE - 1)
    for layer in layers:
        for ty in range(H):
            for tx in range(W):
                t = layer[ty, tx]
                if t and t < len(tdesc) and tdesc[t][1]:
                    fr = tile_px[tdesc[t][0]]
                    dst = full[ty * TILE:(ty + 1) * TILE, tx * TILE:(tx + 1) * TILE]
                    np.copyto(dst, fr, where=fr != 0)
    img = Image.fromarray(rgb.astype(np.uint8)[full], "RGB")
    from PIL import ImageDraw
    d = ImageDraw.Draw(img)
    for k, x, y in objs:
        x, y = x // HALF, y // HALF
        d.rectangle((x, y, x + 15, y + 15), outline=(255, 0, 0))
    if H * TILE > SCREEN_H:
        img = img.crop((0, H * TILE - SCREEN_H, W * TILE, H * TILE))
    return img


if __name__ == "__main__":
    main()
