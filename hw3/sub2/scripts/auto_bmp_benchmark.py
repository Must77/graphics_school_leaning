import os
import re
import struct
import subprocess
from datetime import datetime

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BASE_DIR = os.path.dirname(SCRIPT_DIR)
EXE_PATH = os.path.join(BASE_DIR, "build", "bmp_meanfilter")
TEST_DIR = os.path.join(BASE_DIR, "assets", "test_bmps")
RESULT_MD = os.path.join(BASE_DIR, "docs", "reports", "benchmark_data.md")


def row_stride(width, bpp):
    return ((width * bpp + 31) // 32) * 4


def write_bmp_24(path, width, height, pixel_fn):
    stride = row_stride(width, 24)
    pds = stride * height
    with open(path, "wb") as f:
        f.write(struct.pack("<HIHHI", 0x4D42, 54 + pds, 0, 0, 54))
        f.write(struct.pack("<IIIHHIIIIII", 40, width, height, 1, 24, 0, pds, 0, 0, 0, 0))
        padding = bytes(stride - width * 3)
        for y in range(height - 1, -1, -1):
            row = bytearray()
            for x in range(width):
                r, g, b = pixel_fn(x, y, width, height)
                row.extend((b & 0xFF, g & 0xFF, r & 0xFF))
            f.write(row)
            f.write(padding)


def pattern_gradient(x, y, w, h):
    r = int(255 * x / max(1, w - 1))
    g = int(255 * y / max(1, h - 1))
    b = int((r + g) / 2)
    return r, g, b


def pattern_checker(x, y, w, h):
    block = 32
    v = 220 if ((x // block) + (y // block)) % 2 == 0 else 40
    return v, 255 - v, 140


def pattern_wave(x, y, w, h):
    r = (x * 7 + y * 3) % 256
    g = (x * 5 + y * 11) % 256
    b = (x * 13 + y * 2) % 256
    return r, g, b


def run_filter(input_path, output_path, ksize, kernel, iterations, pad, stride):
    proc = subprocess.run(
        [EXE_PATH, input_path, output_path, str(ksize), kernel, str(iterations), str(pad), str(stride)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="ignore", check=False,
    )
    output = proc.stdout
    elapsed = None
    stats = None

    m = re.search(r"Elapsed time:\s*(\d+)\s*ms", output)
    if m:
        elapsed = int(m.group(1))

    m = re.search(
        r"Output gray statistics:\s*min=(\d+),\s*max=(\d+),\s*mean=([0-9.]+),\s*stddev=([0-9.]+)",
        output,
    )
    if m:
        stats = {
            "min": int(m.group(1)),
            "max": int(m.group(2)),
            "mean": float(m.group(3)),
            "stddev": float(m.group(4)),
        }

    return {"returncode": proc.returncode, "elapsed_ms": elapsed, "stats": stats, "raw_output": output}


def main():
    if not os.path.exists(EXE_PATH):
        raise FileNotFoundError(f"Executable not found: {EXE_PATH}")

    os.makedirs(TEST_DIR, exist_ok=True)
    os.makedirs(os.path.dirname(RESULT_MD), exist_ok=True)

    test_images = [
        ("A_small_256x256.bmp", 256, 256, pattern_gradient),
        ("B_mid_1024x768.bmp", 1024, 768, pattern_checker),
        ("C_large_1920x1080.bmp", 1920, 1080, pattern_wave),
    ]

    # Test configurations: (label, ksize, kernel_preset, iterations, pad_mode, stride)
    configs = [
        ("avg3x3_1x_rep", 3, "avg", 1, 1, 1),
        ("avg3x3_3x_rep", 3, "avg", 3, 1, 1),
        ("avg5x5_1x_rep", 5, "avg", 1, 1, 1),
        ("sharpen3x3_1x_rep", 3, "sharpen", 1, 1, 1),
        ("edge3x3_1x_zero", 3, "edge", 1, 0, 1),
        ("avg3x3_1x_reflect", 3, "avg", 1, 2, 1),
        ("avg3x3_1x_stride2", 3, "avg", 1, 1, 2),
    ]

    rows = []
    for img_name, w, h, fn in test_images:
        in_path = os.path.join(TEST_DIR, img_name)
        write_bmp_24(in_path, w, h, fn)

        for label, ksize, kernel, iters, pad, stride in configs:
            out_name = img_name.replace(".bmp", f"_{label}.bmp")
            out_path = os.path.join(TEST_DIR, out_name)
            res = run_filter(in_path, out_path, ksize, kernel, iters, pad, stride)

            pad_name = {0: "zero", 1: "replicate", 2: "reflect"}[pad]
            rows.append({
                "case": img_name, "resolution": f"{w}x{h}",
                "config": label, "ksize": f"{ksize}x{ksize}",
                "iters": iters, "pad": pad_name, "stride": stride,
                "elapsed_ms": res["elapsed_ms"],
                "min": res["stats"]["min"] if res["stats"] else None,
                "max": res["stats"]["max"] if res["stats"] else None,
                "mean": res["stats"]["mean"] if res["stats"] else None,
                "stddev": res["stats"]["stddev"] if res["stats"] else None,
                "rc": res["returncode"],
            })

    lines = []
    lines.append("# 自动化实验数据\n\n")
    lines.append(f"生成时间：{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n\n")
    lines.append("测试程序：`bmp_meanfilter`\n\n")
    lines.append("| 用例 | 分辨率 | 配置 | 模板 | 迭代 | 填充 | 步长 | 耗时(ms) | min | max | mean | stddev | rc |\n")
    lines.append("|---|---|---|---|---:|---|---:|---:|---:|---:|---:|---:|---:|\n")

    for r in rows:
        mean_s = f"{r['mean']:.4f}" if r['mean'] is not None else "N/A"
        std_s = f"{r['stddev']:.4f}" if r['stddev'] is not None else "N/A"
        lines.append(
            f"| {r['case']} | {r['resolution']} | {r['config']} | {r['ksize']} "
            f"| {r['iters']} | {r['pad']} | {r['stride']} | {r['elapsed_ms']} "
            f"| {r['min']} | {r['max']} | {mean_s} | {std_s} | {r['rc']} |\n"
        )

    lines.append("\n说明：若返回码为 0 表示执行成功。\n")

    with open(RESULT_MD, "w", encoding="utf-8") as f:
        f.writelines(lines)

    print(f"Generated benchmark data: {RESULT_MD}")


if __name__ == "__main__":
    main()
