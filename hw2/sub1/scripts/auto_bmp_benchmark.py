import os
import re
import struct
import subprocess
from datetime import datetime

WORKSPACE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE_PATH = os.path.join(WORKSPACE, "build", "bmp_rotate")
OUT_DIR = os.path.join(WORKSPACE, "assets", "test_bmps")
RESULT_MD = os.path.join(WORKSPACE, "docs", "reports", "benchmark_data.md")


def row_stride(width, bits_per_pixel):
    return ((width * bits_per_pixel + 31) // 32) * 4


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
    r = v
    g = 255 - v
    b = 140
    return r, g, b


def pattern_wave(x, y, w, h):
    r = (x * 7 + y * 3) % 256
    g = (x * 5 + y * 11) % 256
    b = (x * 13 + y * 2) % 256
    return r, g, b


def run_rotate(input_path, angle, output_path):
    proc = subprocess.run(
        [EXE_PATH, input_path, str(angle), output_path],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="ignore",
        cwd=WORKSPACE,
        check=False,
    )
    output = proc.stdout

    elapsed = None
    stats = None

    m_elapsed = re.search(r"Elapsed time:\s*(\d+)\s*ms", output)
    if m_elapsed:
        elapsed = int(m_elapsed.group(1))

    m_stats = re.search(
        r"Output gray statistics:\s*min=(\d+),\s*max=(\d+),\s*mean=([0-9.]+),\s*stddev=([0-9.]+)",
        output,
    )
    if m_stats:
        stats = {
            "min": int(m_stats.group(1)),
            "max": int(m_stats.group(2)),
            "mean": float(m_stats.group(3)),
            "stddev": float(m_stats.group(4)),
        }

    m_dim = re.search(r"Output image:\s*(\d+)\s*x\s*(\d+)", output)
    out_dim = None
    if m_dim:
        out_dim = f"{m_dim.group(1)}x{m_dim.group(2)}"

    return {
        "returncode": proc.returncode,
        "elapsed_ms": elapsed,
        "stats": stats,
        "out_dim": out_dim,
        "raw_output": output,
    }


def main():
    if not os.path.exists(EXE_PATH):
        raise FileNotFoundError(f"Executable not found: {EXE_PATH}")

    os.makedirs(OUT_DIR, exist_ok=True)

    test_cases = [
        ("A_small_256x256.bmp", 256, 256, pattern_gradient),
        ("B_mid_1024x768.bmp", 1024, 768, pattern_checker),
        ("C_large_1920x1080.bmp", 1920, 1080, pattern_wave),
    ]

    angles = [90, 180, 270, 45]

    # Generate source images
    for name, w, h, fn in test_cases:
        in_path = os.path.join(OUT_DIR, name)
        write_bmp_24(in_path, w, h, fn)
        print(f"Generated: {name} ({w}x{h})")

    rows = []
    for name, w, h, fn in test_cases:
        in_path = os.path.join(OUT_DIR, name)
        for angle in angles:
            suffix = f"_rot{angle}"
            out_name = name.replace(".bmp", f"{suffix}.bmp")
            out_path = os.path.join(OUT_DIR, out_name)

            result = run_rotate(in_path, angle, out_path)

            rows.append({
                "case": name,
                "resolution": f"{w}x{h}",
                "angle": angle,
                "elapsed_ms": result["elapsed_ms"],
                "out_dim": result["out_dim"],
                "min": result["stats"]["min"] if result["stats"] else None,
                "max": result["stats"]["max"] if result["stats"] else None,
                "mean": result["stats"]["mean"] if result["stats"] else None,
                "stddev": result["stats"]["stddev"] if result["stats"] else None,
                "rc": result["returncode"],
            })
            print(f"  Rotated {name} by {angle}°: {result['elapsed_ms']} ms, rc={result['returncode']}")

    # Write results markdown
    lines = []
    lines.append("# 自动化实验数据\n")
    lines.append(f"生成时间：{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
    lines.append("测试程序：`bmp_rotate`\n")
    lines.append("测试图片目录：`assets/test_bmps/`\n")
    lines.append("\n")
    lines.append("| 用例 | 原始分辨率 | 旋转角度 | 输出分辨率 | 耗时(ms) | 灰度最小值 | 灰度最大值 | 灰度均值 | 灰度标准差 | 返回码 |\n")
    lines.append("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n")

    for r in rows:
        mean_str = f"{r['mean']:.4f}" if r['mean'] is not None else "N/A"
        std_str = f"{r['stddev']:.4f}" if r['stddev'] is not None else "N/A"
        lines.append(
            f"| {r['case']} | {r['resolution']} | {r['angle']}° | {r['out_dim']} | "
            f"{r['elapsed_ms']} | {r['min']} | {r['max']} | {mean_str} | {std_str} | {r['rc']} |\n"
        )

    lines.append("\n")
    lines.append("说明：若返回码为 0 表示执行成功。90°/180°/270° 使用快速像素映射，45° 使用双线性插值旋转。\n")

    with open(RESULT_MD, "w", encoding="utf-8") as f:
        f.writelines(lines)

    print(f"\nGenerated results: {RESULT_MD}")


if __name__ == "__main__":
    main()
