import os
import struct

WORKSPACE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(WORKSPACE, "assets", "test_bmps")
OUT_DIR = os.path.join(WORKSPACE, "assets", "report_images")
DATA_MD = os.path.join(WORKSPACE, "docs", "reports", "benchmark_data.md")


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
        (bi_size, bi_width, bi_height, bi_planes, bi_bit_count,
         bi_compression, bi_size_image, bi_x, bi_y, bi_clr_used, bi_clr_imp
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
        f.write(struct.pack(
            "<IIIHHIIIIII", 40, width, height, 1, 24, 0, pixel_data_size, 0, 0, 0, 0,
        ))
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


def blit(dst, src_pixels, src_w, src_h, x0, y0):
    dh = len(dst)
    dw = len(dst[0]) if dh > 0 else 0
    for y in range(src_h):
        ty = y0 + y
        if ty < 0 or ty >= dh:
            continue
        for x in range(src_w):
            tx = x0 + x
            if tx < 0 or tx >= dw:
                continue
            dst[ty][tx] = src_pixels[y][x]


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
        for x in range(x0, x1 + 1):
            canvas[y][x] = color


def scale_down(pixels, src_w, src_h, max_dim):
    """Scale image down so max dimension is max_dim, using nearest neighbor."""
    if src_w <= max_dim and src_h <= max_dim:
        return src_w, src_h, pixels

    scale = min(max_dim / src_w, max_dim / src_h)
    new_w = max(1, int(src_w * scale))
    new_h = max(1, int(src_h * scale))

    out = [[(0, 0, 0) for _ in range(new_w)] for _ in range(new_h)]
    for y in range(new_h):
        sy = min(int(y / scale), src_h - 1)
        for x in range(new_w):
            sx = min(int(x / scale), src_w - 1)
            out[y][x] = pixels[sy][sx]

    return new_w, new_h, out


def stitch_horizontal(images, margin=10, sep=6, bg=(245, 245, 245)):
    """Stitch multiple (w, h, pixels) images horizontally with uniform height."""
    if not images:
        return 0, 0, []

    max_h = max(h for w, h, p in images)
    total_w = margin * 2 + sum(w for w, h, p in images) + sep * (len(images) - 1)
    total_h = margin * 2 + max_h
    canvas = make_canvas(total_w, total_h, bg)

    x_cursor = margin
    for w, h, px in images:
        y_off = margin + (max_h - h) // 2
        blit(canvas, px, w, h, x_cursor, y_off)
        x_cursor += w + sep

    return total_w, total_h, canvas


def parse_timing_table(md_path):
    """Parse benchmark_data.md and return list of (case, angle, elapsed_ms)."""
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
            angle = parts[2].replace("°", "")
            elapsed = int(parts[4])
            rows.append((case_name, int(angle), elapsed))
    return rows


def draw_timing_chart(rows, width=1200, height=700):
    bg = (255, 255, 255)
    axis_color = (40, 40, 40)

    colors = {
        90: (220, 90, 90),
        180: (80, 160, 90),
        270: (70, 130, 210),
        45: (200, 160, 50),
    }

    canvas = make_canvas(width, height, bg)

    left = 100
    right = width - 60
    top = 40
    bottom = height - 100

    draw_rect(canvas, left, top, left + 2, bottom, axis_color)
    draw_rect(canvas, left, bottom, right, bottom + 2, axis_color)

    # Group by case
    cases = []
    seen = set()
    for c, a, e in rows:
        if c not in seen:
            cases.append(c)
            seen.add(c)

    all_vals = [e for _, _, e in rows]
    max_v = max(all_vals) if all_vals else 1

    group_count = len(cases)
    plot_w = right - left
    group_w = max(60, plot_w // max(1, group_count))

    angles_list = sorted(set(a for _, a, _ in rows))
    n_bars = len(angles_list)
    bar_w = max(6, group_w // (n_bars + 2))

    for gi, case in enumerate(cases):
        gx_center = left + gi * group_w + group_w // 2
        case_rows = [(a, e) for c, a, e in rows if c == case]

        for bi, (angle, elapsed) in enumerate(case_rows):
            bar_h = int((elapsed / max_v) * (bottom - top)) if max_v else 0
            x0 = gx_center - (n_bars * bar_w) // 2 + bi * bar_w + 2
            x1 = x0 + bar_w - 3
            y0 = bottom - bar_h
            color = colors.get(angle, (150, 150, 150))
            draw_rect(canvas, x0, y0, x1, bottom - 1, color)

    return width, height, canvas


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    case_prefixes = [
        "A_small_256x256",
        "B_mid_1024x768",
        "C_large_1920x1080",
    ]
    angles = [90, 180, 270, 45]
    max_thumb = 200  # max thumbnail dimension for comparison strips

    for p in case_prefixes:
        src_path = os.path.join(SRC_DIR, f"{p}.bmp")
        src_w, src_h, src_px = read_bmp_24(src_path)

        images = [scale_down(src_px, src_w, src_h, max_thumb)]
        for angle in angles:
            rot_path = os.path.join(SRC_DIR, f"{p}_rot{angle}.bmp")
            rw, rh, rpx = read_bmp_24(rot_path)
            images.append(scale_down(rpx, rw, rh, max_thumb))

        w, h, canvas = stitch_horizontal(images)
        out_path = os.path.join(OUT_DIR, f"compare_{p}.bmp")
        write_bmp_24(out_path, w, h, canvas)
        print(f"Generated: compare_{p}.bmp ({w}x{h})")

    # Timing chart
    timing_rows = parse_timing_table(DATA_MD)
    w, h, chart = draw_timing_chart(timing_rows)
    out_path = os.path.join(OUT_DIR, "timing_chart.bmp")
    write_bmp_24(out_path, w, h, chart)
    print(f"Generated: timing_chart.bmp ({w}x{h})")

    print(f"\nAll report images saved to: {OUT_DIR}")


if __name__ == "__main__":
    main()
