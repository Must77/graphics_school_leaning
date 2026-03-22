import os
import re
import struct

WORKSPACE = r"C:\Users\Must77\Downloads\graphics"
SRC_DIR = os.path.join(WORKSPACE, "auto_generated")
OUT_DIR = os.path.join(WORKSPACE, "report_images")
DATA_MD = os.path.join(WORKSPACE, "自动化实验数据.md")


def row_stride(width, bpp):
    return ((width * bpp + 31) // 32) * 4


def read_bmp_24(path):
    with open(path, "rb") as f:
        file_header = f.read(14)
        if len(file_header) != 14:
            raise ValueError(f"Invalid BMP file header: {path}")

        bf_type, bf_size, bf_r1, bf_r2, bf_off_bits = struct.unpack("<HIHHI", file_header)
        if bf_type != 0x4D42:
            raise ValueError(f"Not BMP: {path}")

        info = f.read(40)
        if len(info) != 40:
            raise ValueError(f"Invalid BMP info header: {path}")

        (
            bi_size,
            bi_width,
            bi_height,
            bi_planes,
            bi_bit_count,
            bi_compression,
            bi_size_image,
            bi_x,
            bi_y,
            bi_clr_used,
            bi_clr_imp,
        ) = struct.unpack("<IIIHHIIIIII", info)

        if bi_bit_count != 24 or bi_compression != 0:
            raise ValueError(f"Only 24-bit BI_RGB BMP supported: {path}")

        width = int(bi_width)
        height = int(bi_height)
        if width <= 0 or height <= 0:
            raise ValueError(f"Invalid dimensions in {path}")

        stride = row_stride(width, 24)
        f.seek(bf_off_bits)

        pixels = [[(0, 0, 0) for _ in range(width)] for _ in range(height)]
        for y in range(height - 1, -1, -1):
            row = f.read(stride)
            if len(row) != stride:
                raise ValueError(f"Truncated pixel row in {path}")
            for x in range(width):
                b = row[x * 3 + 0]
                g = row[x * 3 + 1]
                r = row[x * 3 + 2]
                pixels[y][x] = (r, g, b)

        return width, height, pixels


def write_bmp_24(path, width, height, pixels):
    stride = row_stride(width, 24)
    pixel_data_size = stride * height
    bf_off_bits = 14 + 40
    bf_size = bf_off_bits + pixel_data_size

    with open(path, "wb") as f:
        f.write(struct.pack("<HIHHI", 0x4D42, bf_size, 0, 0, bf_off_bits))
        f.write(
            struct.pack(
                "<IIIHHIIIIII",
                40,
                width,
                height,
                1,
                24,
                0,
                pixel_data_size,
                0,
                0,
                0,
                0,
            )
        )

        pad = bytes(stride - width * 3)
        for y in range(height - 1, -1, -1):
            row = bytearray()
            for x in range(width):
                r, g, b = pixels[y][x]
                row.extend((b & 0xFF, g & 0xFF, r & 0xFF))
            f.write(row)
            f.write(pad)


def make_canvas(width, height, color=(255, 255, 255)):
    return [[color for _ in range(width)] for _ in range(height)]


def blit(dst, src, x0, y0):
    h = len(src)
    w = len(src[0]) if h > 0 else 0
    dh = len(dst)
    dw = len(dst[0]) if dh > 0 else 0
    for y in range(h):
        ty = y0 + y
        if ty < 0 or ty >= dh:
            continue
        for x in range(w):
            tx = x0 + x
            if tx < 0 or tx >= dw:
                continue
            dst[ty][tx] = src[y][x]


def draw_rect(canvas, x0, y0, x1, y1, color):
    h = len(canvas)
    w = len(canvas[0]) if h > 0 else 0
    x0 = max(0, min(w - 1, x0))
    x1 = max(0, min(w - 1, x1))
    y0 = max(0, min(h - 1, y0))
    y1 = max(0, min(h - 1, y1))
    if x0 > x1 or y0 > y1:
        return
    for y in range(y0, y1 + 1):
        row = canvas[y]
        for x in range(x0, x1 + 1):
            row[x] = color


def stitch_triplet(img_a, img_b, img_c):
    wa, ha, pa = img_a
    wb, hb, pb = img_b
    wc, hc, pc = img_c
    if wa != wb or wb != wc or ha != hb or hb != hc:
        raise ValueError("Images to stitch must share dimensions")

    margin = 20
    sep = 8
    width = margin * 2 + wa * 3 + sep * 2
    height = margin * 2 + ha
    out = make_canvas(width, height, (245, 245, 245))

    blit(out, pa, margin, margin)
    draw_rect(out, margin + wa, margin, margin + wa + sep - 1, margin + ha - 1, (255, 255, 255))

    x2 = margin + wa + sep
    blit(out, pb, x2, margin)
    draw_rect(out, x2 + wb, margin, x2 + wb + sep - 1, margin + ha - 1, (255, 255, 255))

    x3 = x2 + wb + sep
    blit(out, pc, x3, margin)
    return width, height, out


def compute_gray_histogram(gray_pixels):
    hist = [0] * 256
    h = len(gray_pixels)
    w = len(gray_pixels[0]) if h > 0 else 0
    for y in range(h):
        for x in range(w):
            r, g, b = gray_pixels[y][x]
            # Gray output should have r=g=b; robust fallback still computes luminance.
            val = int(0.299 * r + 0.587 * g + 0.114 * b + 0.5)
            val = max(0, min(255, val))
            hist[val] += 1
    return hist


def draw_histogram_bmp(hist, width=1024, height=512):
    bg = (255, 255, 255)
    axis = (40, 40, 40)
    bar = (70, 130, 210)

    canvas = make_canvas(width, height, bg)

    left = 60
    right = width - 30
    top = 20
    bottom = height - 50

    draw_rect(canvas, left, top, left + 1, bottom, axis)
    draw_rect(canvas, left, bottom, right, bottom + 1, axis)

    max_count = max(hist) if hist else 1
    plot_w = right - left
    plot_h = bottom - top

    for i in range(256):
        x0 = left + int(i * plot_w / 256)
        x1 = left + int((i + 1) * plot_w / 256) - 1
        if x1 < x0:
            x1 = x0
        bar_h = int((hist[i] / max_count) * plot_h) if max_count else 0
        y0 = bottom - bar_h
        draw_rect(canvas, x0, y0, x1, bottom - 1, bar)

    return width, height, canvas


def parse_timing_table(md_path):
    rows = []
    with open(md_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line.startswith("|"):
                continue
            if "用例" in line or "---" in line:
                continue
            parts = [p.strip() for p in line.strip("|").split("|")]
            if len(parts) < 10:
                continue
            case_name = parts[0]
            inv_ms = int(parts[2])
            gray_ms = int(parts[3])
            rows.append((case_name, inv_ms, gray_ms))
    return rows


def draw_timing_chart(rows, width=1200, height=700):
    bg = (255, 255, 255)
    axis = (40, 40, 40)
    inv_color = (220, 90, 90)
    gray_color = (80, 160, 90)

    canvas = make_canvas(width, height, bg)

    left = 100
    right = width - 60
    top = 40
    bottom = height - 100

    draw_rect(canvas, left, top, left + 2, bottom, axis)
    draw_rect(canvas, left, bottom, right, bottom + 2, axis)

    all_vals = [v for row in rows for v in row[1:]]
    max_v = max(all_vals) if all_vals else 1

    group_count = len(rows)
    plot_w = right - left
    group_w = max(20, plot_w // max(1, group_count))
    bar_w = max(8, group_w // 3)

    for i, (_, inv, gray) in enumerate(rows):
        gx = left + i * group_w + group_w // 2

        inv_h = int((inv / max_v) * (bottom - top)) if max_v else 0
        gray_h = int((gray / max_v) * (bottom - top)) if max_v else 0

        draw_rect(canvas, gx - bar_w - 4, bottom - inv_h, gx - 5, bottom - 1, inv_color)
        draw_rect(canvas, gx + 5, bottom - gray_h, gx + bar_w + 4, bottom - 1, gray_color)

    return width, height, canvas


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    case_prefixes = [
        "A_small_256x256",
        "B_mid_1024x768",
        "C_large_1920x1080",
    ]

    for p in case_prefixes:
        src = read_bmp_24(os.path.join(SRC_DIR, f"{p}.bmp"))
        inv = read_bmp_24(os.path.join(SRC_DIR, f"{p}_invert.bmp"))
        gry = read_bmp_24(os.path.join(SRC_DIR, f"{p}_gray.bmp"))
        w, h, out = stitch_triplet(src, inv, gry)
        write_bmp_24(os.path.join(OUT_DIR, f"compare_{p}.bmp"), w, h, out)

    # Histogram from gray image of sample B (mid-size)
    _, _, gray_b = read_bmp_24(os.path.join(SRC_DIR, "B_mid_1024x768_gray.bmp"))
    hist = compute_gray_histogram(gray_b)
    w, h, hist_img = draw_histogram_bmp(hist)
    write_bmp_24(os.path.join(OUT_DIR, "histogram_B_gray.bmp"), w, h, hist_img)

    # Timing chart from markdown table
    rows = parse_timing_table(DATA_MD)
    w, h, chart = draw_timing_chart(rows)
    write_bmp_24(os.path.join(OUT_DIR, "timing_chart.bmp"), w, h, chart)

    print("Generated report images in:", OUT_DIR)


if __name__ == "__main__":
    main()
