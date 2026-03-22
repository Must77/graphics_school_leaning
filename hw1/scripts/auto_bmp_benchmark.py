import os
import re
import struct
import subprocess
from datetime import datetime

WORKSPACE = r"C:\Users\Must77\Downloads\graphics"
EXE_PATH = os.path.join(WORKSPACE, "bmp_lab_vs.exe")
OUT_DIR = os.path.join(WORKSPACE, "auto_generated")
RESULT_MD = os.path.join(WORKSPACE, "自动化实验数据.md")


def row_stride(width, bits_per_pixel):
    return ((width * bits_per_pixel + 31) // 32) * 4


def write_bmp_24(path, width, height, pixel_fn):
    stride = row_stride(width, 24)
    pixel_data_size = stride * height
    bf_type = 0x4D42
    bf_off_bits = 14 + 40
    bf_size = bf_off_bits + pixel_data_size

    file_header = struct.pack("<HIHHI", bf_type, bf_size, 0, 0, bf_off_bits)
    info_header = struct.pack(
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
    g = (255 - v)
    b = 140
    return r, g, b


def pattern_wave(x, y, w, h):
    # Integer-only synthetic wave-like pattern
    r = (x * 7 + y * 3) % 256
    g = (x * 5 + y * 11) % 256
    b = (x * 13 + y * 2) % 256
    return r, g, b


def run_mode(input_path, mode, output_path):
    proc = subprocess.run(
        [EXE_PATH, input_path, str(mode), output_path],
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

    return {
        "returncode": proc.returncode,
        "elapsed_ms": elapsed,
        "stats": stats,
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

    rows = []
    for name, w, h, fn in test_cases:
        in_path = os.path.join(OUT_DIR, name)
        write_bmp_24(in_path, w, h, fn)

        inv_out = os.path.join(OUT_DIR, name.replace(".bmp", "_invert.bmp"))
        gray_out = os.path.join(OUT_DIR, name.replace(".bmp", "_gray.bmp"))

        inv = run_mode(in_path, 1, inv_out)
        gray = run_mode(in_path, 2, gray_out)

        rows.append(
            {
                "case": name,
                "resolution": f"{w}x{h}",
                "invert_ms": inv["elapsed_ms"],
                "gray_ms": gray["elapsed_ms"],
                "min": None if not gray["stats"] else gray["stats"]["min"],
                "max": None if not gray["stats"] else gray["stats"]["max"],
                "mean": None if not gray["stats"] else gray["stats"]["mean"],
                "stddev": None if not gray["stats"] else gray["stats"]["stddev"],
                "invert_rc": inv["returncode"],
                "gray_rc": gray["returncode"],
            }
        )

    lines = []
    lines.append("# 自动化实验数据\n")
    lines.append(f"生成时间：{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
    lines.append("测试程序：`bmp_lab_vs.exe`\n")
    lines.append("测试图片目录：`auto_generated/`\n")
    lines.append("\n")
    lines.append("| 用例 | 分辨率 | 反色耗时(ms) | 灰度耗时(ms) | 灰度最小值 | 灰度最大值 | 灰度均值 | 灰度标准差 | invert返回码 | gray返回码 |\n")
    lines.append("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n")

    for r in rows:
        lines.append(
            f"| {r['case']} | {r['resolution']} | {r['invert_ms']} | {r['gray_ms']} | {r['min']} | {r['max']} | {r['mean']:.4f} | {r['stddev']:.4f} | {r['invert_rc']} | {r['gray_rc']} |\n"
        )

    lines.append("\n")
    lines.append("说明：若返回码为 0 表示执行成功。该表可直接用于填写实验报告中的量化分析部分。\n")

    with open(RESULT_MD, "w", encoding="utf-8") as f:
        f.writelines(lines)

    print(f"Generated results: {RESULT_MD}")


if __name__ == "__main__":
    main()
