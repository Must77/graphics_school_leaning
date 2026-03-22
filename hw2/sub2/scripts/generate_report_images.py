import os
import struct

WORKSPACE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(WORKSPACE, "assets", "test_bmps")
OUT_DIR = os.path.join(WORKSPACE, "assets", "report_images")
DATA_MD = os.path.join(WORKSPACE, "docs", "reports", "benchmark_data.md")


def row_stride(w, bpp):
    return ((w * bpp + 31) // 32) * 4


def read_bmp_24(path):
    with open(path, "rb") as f:
        fh = f.read(14)
        bf_type, bf_size, _, _, bf_off = struct.unpack("<HIHHI", fh)
        if bf_type != 0x4D42:
            raise ValueError(f"Not BMP: {path}")
        info = f.read(40)
        vals = struct.unpack("<IIIHHIIIIII", info)
        w, h, bpp = int(vals[1]), int(vals[2]), vals[4]
        if bpp != 24:
            raise ValueError(f"Only 24-bit: {path}")
        stride = row_stride(w, 24)
        f.seek(bf_off)
        pixels = [[(0,0,0)] * w for _ in range(h)]
        for y in range(h - 1, -1, -1):
            row = f.read(stride)
            for x in range(w):
                pixels[y][x] = (row[x*3+2], row[x*3+1], row[x*3])
        return w, h, pixels


def write_bmp_24(path, w, h, pixels):
    stride = row_stride(w, 24)
    pds = stride * h
    off = 54
    with open(path, "wb") as f:
        f.write(struct.pack("<HIHHI", 0x4D42, off + pds, 0, 0, off))
        f.write(struct.pack("<IIIHHIIIIII", 40, w, h, 1, 24, 0, pds, 0, 0, 0, 0))
        pad = bytes(stride - w * 3)
        for y in range(h - 1, -1, -1):
            row = bytearray()
            for x in range(w):
                r, g, b = pixels[y][x]
                row.extend((b & 0xFF, g & 0xFF, r & 0xFF))
            f.write(row)
            f.write(pad)


def make_canvas(w, h, color=(255,255,255)):
    return [[color]*w for _ in range(h)]


def blit(dst, src, sw, sh, x0, y0):
    dh, dw = len(dst), len(dst[0])
    for y in range(sh):
        ty = y0 + y
        if ty < 0 or ty >= dh: continue
        for x in range(sw):
            tx = x0 + x
            if tx < 0 or tx >= dw: continue
            dst[ty][tx] = src[y][x]


def draw_rect(c, x0, y0, x1, y1, color):
    h, w = len(c), len(c[0])
    for y in range(max(0,y0), min(h,y1+1)):
        for x in range(max(0,x0), min(w,x1+1)):
            c[y][x] = color


def scale_down(px, sw, sh, max_dim):
    if sw <= max_dim and sh <= max_dim:
        return sw, sh, px
    s = min(max_dim/sw, max_dim/sh)
    nw, nh = max(1, int(sw*s)), max(1, int(sh*s))
    out = [[(0,0,0)]*nw for _ in range(nh)]
    for y in range(nh):
        sy = min(int(y/s), sh-1)
        for x in range(nw):
            sx = min(int(x/s), sw-1)
            out[y][x] = px[sy][sx]
    return nw, nh, out


def stitch_horizontal(images, margin=10, sep=6, bg=(245,245,245)):
    if not images: return 0, 0, []
    max_h = max(h for w,h,p in images)
    tw = margin*2 + sum(w for w,h,p in images) + sep*(len(images)-1)
    th = margin*2 + max_h
    canvas = make_canvas(tw, th, bg)
    xc = margin
    for w, h, px in images:
        yo = margin + (max_h - h) // 2
        blit(canvas, px, w, h, xc, yo)
        xc += w + sep
    return tw, th, canvas


def parse_timing(md_path):
    rows = []
    with open(md_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line.startswith("|") or "用例" in line or "---" in line:
                continue
            parts = [p.strip() for p in line.strip("|").split("|")]
            if len(parts) < 11:
                continue
            rows.append((parts[0], parts[3], float(parts[4]), int(parts[5])))
    return rows  # (case, position, opacity, elapsed)


def draw_timing_chart(rows, width=1200, height=700):
    bg = (255,255,255)
    axis = (40,40,40)
    colors = [(220,90,90), (80,160,90), (70,130,210), (200,160,50)]

    canvas = make_canvas(width, height, bg)
    left, right, top, bottom = 100, width-60, 40, height-100
    draw_rect(canvas, left, top, left+2, bottom, axis)
    draw_rect(canvas, left, bottom, right, bottom+2, axis)

    # Group by case
    cases = []
    seen = set()
    for c, pos, opa, e in rows:
        if c not in seen:
            cases.append(c)
            seen.add(c)

    all_vals = [e for _,_,_,e in rows]
    max_v = max(all_vals) if all_vals else 1
    plot_w = right - left
    group_w = max(60, plot_w // max(1, len(cases)))

    configs_per_case = len(rows) // max(1, len(cases))
    bar_w = max(6, group_w // (configs_per_case + 2))

    for gi, case in enumerate(cases):
        gx = left + gi * group_w + group_w // 2
        case_rows = [(pos, opa, e) for c, pos, opa, e in rows if c == case]
        for bi, (pos, opa, elapsed) in enumerate(case_rows):
            bh = int((elapsed / max_v) * (bottom - top)) if max_v else 0
            x0 = gx - (len(case_rows) * bar_w) // 2 + bi * bar_w + 2
            x1 = x0 + bar_w - 3
            draw_rect(canvas, x0, bottom - bh, x1, bottom - 1, colors[bi % len(colors)])

    return width, height, canvas


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    prefixes = ["A_small_256x256", "B_mid_1024x768", "C_large_1920x1080"]
    # For each case, show: original, center_o30, tile_o30, br_o50, center_o70
    suffixes = ["", "_wm_center_o30", "_wm_tile_o30", "_wm_br_o50", "_wm_center_o70"]
    max_thumb = 200

    for p in prefixes:
        images = []
        for s in suffixes:
            path = os.path.join(SRC_DIR, f"{p}{s}.bmp")
            w, h, px = read_bmp_24(path)
            images.append(scale_down(px, w, h, max_thumb))
        tw, th, canvas = stitch_horizontal(images)
        out = os.path.join(OUT_DIR, f"compare_{p}.bmp")
        write_bmp_24(out, tw, th, canvas)
        print(f"Generated: compare_{p}.bmp ({tw}x{th})")

    # Timing chart
    timing = parse_timing(DATA_MD)
    w, h, chart = draw_timing_chart(timing)
    out = os.path.join(OUT_DIR, "timing_chart.bmp")
    write_bmp_24(out, w, h, chart)
    print(f"Generated: timing_chart.bmp ({w}x{h})")

    print(f"\nAll images saved to: {OUT_DIR}")


if __name__ == "__main__":
    main()
