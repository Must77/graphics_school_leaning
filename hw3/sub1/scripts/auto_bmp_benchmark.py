import os
import re
import struct
import subprocess
from datetime import datetime

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BASE_DIR = os.path.dirname(SCRIPT_DIR)
EXE_PATH = os.path.join(BASE_DIR, "build", "bmp_enhance")
TEST_DIR = os.path.join(BASE_DIR, "assets", "test_bmps")
RESULT_MD = os.path.join(BASE_DIR, "docs", "reports", "benchmark_data.md")


def row_stride(width, bpp):
    return ((width * bpp + 31) // 32) * 4


def write_bmp_24(path, width, height, pixel_fn):
    stride = row_stride(width, 24)
    pixel_data_size = stride * height
    bf_off_bits = 14 + 40
    bf_size = bf_off_bits + pixel_data_size

    file_header = struct.pack("<HIHHI", 0x4D42, bf_size, 0, 0, bf_off_bits)
    info_header = struct.pack(
        "<IIIHHIIIIII", 40, width, height, 1, 24, 0, pixel_data_size, 0, 0, 0, 0,
    )

    with open(path, "wb") as f:
        f.write(file_header)
        f.write(info_header)
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


def run_mode(input_path, mode, output_path):
    proc = subprocess.run(
        [EXE_PATH, input_path, str(mode), output_path],
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


MODE_NAMES = {1: "Log", 2: "PowerLaw(0.4)", 3: "PowerLaw(2.5)", 4: "HistEq"}


def main():
    if not os.path.exists(EXE_PATH):
        raise FileNotFoundError(f"Executable not found: {EXE_PATH}")

    os.makedirs(TEST_DIR, exist_ok=True)
    os.makedirs(os.path.dirname(RESULT_MD), exist_ok=True)

    test_cases = [
        ("A_small_256x256.bmp", 256, 256, pattern_gradient),
        ("B_mid_1024x768.bmp", 1024, 768, pattern_checker),
        ("C_large_1920x1080.bmp", 1920, 1080, pattern_wave),
    ]

    rows = []
    for name, w, h, fn in test_cases:
        in_path = os.path.join(TEST_DIR, name)
        write_bmp_24(in_path, w, h, fn)

        for mode in [1, 2, 3, 4]:
            suffix = {1: "_log", 2: "_pow04", 3: "_pow25", 4: "_histeq"}[mode]
            out_path = os.path.join(TEST_DIR, name.replace(".bmp", f"{suffix}.bmp"))
            res = run_mode(in_path, mode, out_path)
            rows.append({
                "case": name, "resolution": f"{w}x{h}",
                "mode": MODE_NAMES[mode],
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
    lines.append("测试程序：`bmp_enhance`\n\n")
    lines.append("| 用例 | 分辨率 | 增强方式 | 耗时(ms) | 灰度最小值 | 灰度最大值 | 灰度均值 | 灰度标准差 | 返回码 |\n")
    lines.append("|---|---|---|---:|---:|---:|---:|---:|---:|\n")

    for r in rows:
        mean_str = f"{r['mean']:.4f}" if r['mean'] is not None else "N/A"
        std_str = f"{r['stddev']:.4f}" if r['stddev'] is not None else "N/A"
        lines.append(
            f"| {r['case']} | {r['resolution']} | {r['mode']} | {r['elapsed_ms']} "
            f"| {r['min']} | {r['max']} | {mean_str} | {std_str} | {r['rc']} |\n"
        )

    lines.append("\n说明：若返回码为 0 表示执行成功。\n")

    with open(RESULT_MD, "w", encoding="utf-8") as f:
        f.writelines(lines)

    print(f"Generated benchmark data: {RESULT_MD}")


if __name__ == "__main__":
    main()
