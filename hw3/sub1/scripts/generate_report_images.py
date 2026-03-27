import os
import struct
import math

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BASE_DIR = os.path.dirname(SCRIPT_DIR)
SRC_DIR = os.path.join(BASE_DIR, "assets", "test_bmps")
OUT_DIR = os.path.join(BASE_DIR, "assets", "report_images")
DATA_MD = os.path.join(BASE_DIR, "docs", "reports", "benchmark_data.md")


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
        w, h, bpp = vals[1], vals[2], vals[4]
        if bpp != 24:
            raise ValueError(f"Only 24-bit supported: {path}")
        stride = row_stride(w, 24)
        f.seek(bf_off)
        pixels = [[(0, 0, 0)] * w for _ in range(h)]
        for y in range(h - 1, -1, -1):
            row = f.read(stride)
            for x in range(w):
                b, g, r = row[x*3], row[x*3+1], row[x*3+2]
                pixels[y][x] = (r, g, b)
    return w, h, pixels


def write_bmp_24(path, w, h, pixels):
    stride = row_stride(w, 24)
    pds = stride * h
    with open(path, "wb") as f:
        f.write(struct.pack("<HIHHI", 0x4D42, 54 + pds, 0, 0, 54))
        f.write(struct.pack("<IIIHHIIIIII", 40, w, h, 1, 24, 0, pds, 0, 0, 0, 0))
        pad = bytes(stride - w * 3)
        for y in range(h - 1, -1, -1):
            row = bytearray()
            for x in range(w):
                r, g, b = pixels[y][x]
                row.extend((b & 0xFF, g & 0xFF, r & 0xFF))
            f.write(row)
            f.write(pad)


def make_canvas(w, h, color=(255, 255, 255)):
    return [[color] * w for _ in range(h)]


def blit(dst, src, x0, y0):
    dh, dw = len(dst), len(dst[0])
    sh, sw = len(src), len(src[0])
    for y in range(sh):
        ty = y0 + y
        if ty < 0 or ty >= dh:
            continue
        for x in range(sw):
            tx = x0 + x
            if tx < 0 or tx >= dw:
                continue
            dst[ty][tx] = src[y][x]


def draw_rect(canvas, x0, y0, x1, y1, color):
    h, w = len(canvas), len(canvas[0])
    for y in range(max(0, y0), min(h, y1 + 1)):
        for x in range(max(0, x0), min(w, x1 + 1)):
            canvas[y][x] = color


def resize_half(pixels, w, h):
    nw, nh = w // 2, h // 2
    out = [[(0,0,0)] * nw for _ in range(nh)]
    for y in range(nh):
        for x in range(nw):
            r, g, b = pixels[y*2][x*2]
            out[y][x] = (r, g, b)
    return nw, nh, out


def stitch_row(images, margin=10, sep=6, bg=(245, 245, 245)):
    # images: list of (w, h, pixels)
    max_h = max(h for _, h, _ in images)
    total_w = margin * 2 + sum(w for w, _, _ in images) + sep * (len(images) - 1)
    canvas = make_canvas(total_w, max_h + margin * 2, bg)
    cx = margin
    for w, h, px in images:
        blit(canvas, px, cx, margin + (max_h - h) // 2)
        cx += w + sep
    return total_w, max_h + margin * 2, canvas


def compute_histogram(pixels):
    hist = [0] * 256
    for row in pixels:
        for r, g, b in row:
            val = int(0.299 * r + 0.587 * g + 0.114 * b + 0.5)
            hist[max(0, min(255, val))] += 1
    return hist


def draw_histogram(hist, width=512, height=300):
    canvas = make_canvas(width, height, (255, 255, 255))
    left, right, top, bottom = 40, width - 10, 10, height - 30
    draw_rect(canvas, left, top, left + 1, bottom, (40, 40, 40))
    draw_rect(canvas, left, bottom, right, bottom + 1, (40, 40, 40))

    max_c = max(hist) if hist else 1
    pw = right - left
    ph = bottom - top

    for i in range(256):
        x0 = left + int(i * pw / 256)
        x1 = left + int((i + 1) * pw / 256) - 1
        if x1 < x0: x1 = x0
        bh = int((hist[i] / max_c) * ph) if max_c else 0
        draw_rect(canvas, x0, bottom - bh, x1, bottom - 1, (70, 130, 210))
    return width, height, canvas


def parse_timing(md_path):
    rows = []
    with open(md_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line.startswith("|") or "用例" in line or "---" in line:
                continue
            parts = [p.strip() for p in line.strip("|").split("|")]
            if len(parts) < 9:
                continue
            try:
                rows.append((parts[0], parts[2], int(parts[3])))
            except (ValueError, IndexError):
                continue
    return rows


def draw_timing_chart(rows, width=1200, height=600):
    canvas = make_canvas(width, height, (255, 255, 255))
    left, right, top, bottom = 80, width - 40, 30, height - 60
    draw_rect(canvas, left, top, left + 2, bottom, (40, 40, 40))
    draw_rect(canvas, left, bottom, right, bottom + 2, (40, 40, 40))

    if not rows:
        return width, height, canvas

    max_v = max(v for _, _, v in rows) or 1
    n = len(rows)
    gw = max(20, (right - left) // max(1, n))
    bw = max(6, gw // 2)

    colors = {
        "Log": (220, 90, 90), "PowerLaw(0.4)": (80, 160, 90),
        "PowerLaw(2.5)": (90, 90, 220), "HistEq": (200, 160, 50),
    }

    for i, (case, mode, ms) in enumerate(rows):
        gx = left + i * gw + gw // 2
        bar_h = int((ms / max_v) * (bottom - top))
        c = colors.get(mode, (120, 120, 120))
        draw_rect(canvas, gx - bw // 2, bottom - bar_h, gx + bw // 2, bottom - 1, c)

    return width, height, canvas


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    prefixes = ["A_small_256x256", "B_mid_1024x768", "C_large_1920x1080"]
    suffixes = ["", "_log", "_pow04", "_pow25", "_histeq"]

    for p in prefixes:
        images = []
        for s in suffixes:
            fpath = os.path.join(SRC_DIR, f"{p}{s}.bmp")
            if not os.path.exists(fpath):
                continue
            w, h, px = read_bmp_24(fpath)
            # Resize large images for comparison
            while w > 512:
                w, h, px = resize_half(px, w, h)
            images.append((w, h, px))

        if len(images) >= 2:
            tw, th, canvas = stitch_row(images)
            write_bmp_24(os.path.join(OUT_DIR, f"compare_{p}.bmp"), tw, th, canvas)

    # Histogram before/after for sample B
    b_orig = os.path.join(SRC_DIR, "B_mid_1024x768.bmp")
    b_histeq = os.path.join(SRC_DIR, "B_mid_1024x768_histeq.bmp")
    if os.path.exists(b_orig) and os.path.exists(b_histeq):
        _, _, px_orig = read_bmp_24(b_orig)
        _, _, px_eq = read_bmp_24(b_histeq)
        h1 = compute_histogram(px_orig)
        h2 = compute_histogram(px_eq)
        w1, h1h, c1 = draw_histogram(h1)
        w2, h2h, c2 = draw_histogram(h2)
        tw, th, canvas = stitch_row([(w1, h1h, c1), (w2, h2h, c2)])
        write_bmp_24(os.path.join(OUT_DIR, "histogram_before_after.bmp"), tw, th, canvas)

    # Timing chart
    if os.path.exists(DATA_MD):
        rows = parse_timing(DATA_MD)
        w, h, chart = draw_timing_chart(rows)
        write_bmp_24(os.path.join(OUT_DIR, "timing_chart.bmp"), w, h, chart)

    print(f"Generated report images in: {OUT_DIR}")


if __name__ == "__main__":
    main()
