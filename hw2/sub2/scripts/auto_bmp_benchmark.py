import os
import re
import struct
import subprocess
from datetime import datetime

WORKSPACE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE_PATH = os.path.join(WORKSPACE, "build", "bmp_watermark")
OUT_DIR = os.path.join(WORKSPACE, "assets", "test_bmps")
RESULT_MD = os.path.join(WORKSPACE, "docs", "reports", "benchmark_data.md")


def row_stride(width, bpp):
    return ((width * bpp + 31) // 32) * 4


def write_bmp_24(path, width, height, pixel_fn):
    stride = row_stride(width, 24)
    pds = stride * height
    off = 14 + 40
    with open(path, "wb") as f:
        f.write(struct.pack("<HIHHI", 0x4D42, off + pds, 0, 0, off))
        f.write(struct.pack("<IIIHHIIIIII", 40, width, height, 1, 24, 0, pds, 0, 0, 0, 0))
        pad = bytes(stride - width * 3)
        for y in range(height - 1, -1, -1):
            row = bytearray()
            for x in range(width):
                r, g, b = pixel_fn(x, y, width, height)
                row.extend((b & 0xFF, g & 0xFF, r & 0xFF))
            f.write(row)
            f.write(pad)


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
    return (x * 7 + y * 3) % 256, (x * 5 + y * 11) % 256, (x * 13 + y * 2) % 256


def run_watermark(input_path, text, output_path, opacity, position):
    proc = subprocess.run(
        [EXE_PATH, input_path, text, output_path, str(opacity), position],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", errors="ignore",
        cwd=WORKSPACE, check=False,
    )
    out = proc.stdout
    elapsed = None
    m = re.search(r"Elapsed time:\s*(\d+)\s*ms", out)
    if m:
        elapsed = int(m.group(1))

    stats = None
    m = re.search(
        r"Output gray statistics:\s*min=(\d+),\s*max=(\d+),\s*mean=([0-9.]+),\s*stddev=([0-9.]+)", out)
    if m:
        stats = {"min": int(m.group(1)), "max": int(m.group(2)),
                 "mean": float(m.group(3)), "stddev": float(m.group(4))}
    return {"rc": proc.returncode, "elapsed_ms": elapsed, "stats": stats}


def main():
    if not os.path.exists(EXE_PATH):
        raise FileNotFoundError(f"Executable not found: {EXE_PATH}")
    os.makedirs(OUT_DIR, exist_ok=True)

    test_cases = [
        ("A_small_256x256.bmp", 256, 256, pattern_gradient),
        ("B_mid_1024x768.bmp", 1024, 768, pattern_checker),
        ("C_large_1920x1080.bmp", 1920, 1080, pattern_wave),
    ]

    configs = [
        ("center", 0.3, "SAMPLE"),
        ("tile",   0.3, "WATERMARK"),
        ("br",     0.5, "COPYRIGHT"),
        ("center", 0.7, "SAMPLE"),
    ]

    # Generate source images
    for name, w, h, fn in test_cases:
        write_bmp_24(os.path.join(OUT_DIR, name), w, h, fn)
        print(f"Generated: {name} ({w}x{h})")

    rows = []
    for name, w, h, fn in test_cases:
        in_path = os.path.join(OUT_DIR, name)
        for pos, opa, txt in configs:
            suffix = f"_wm_{pos}_o{int(opa*100)}"
            out_name = name.replace(".bmp", f"{suffix}.bmp")
            out_path = os.path.join(OUT_DIR, out_name)
            r = run_watermark(in_path, txt, out_path, opa, pos)
            rows.append({
                "case": name, "resolution": f"{w}x{h}",
                "text": txt, "position": pos, "opacity": opa,
                "elapsed_ms": r["elapsed_ms"],
                "min": r["stats"]["min"] if r["stats"] else None,
                "max": r["stats"]["max"] if r["stats"] else None,
                "mean": r["stats"]["mean"] if r["stats"] else None,
                "stddev": r["stats"]["stddev"] if r["stats"] else None,
                "rc": r["rc"],
            })
            print(f"  {name} pos={pos} opa={opa} text={txt}: {r['elapsed_ms']} ms, rc={r['rc']}")

    lines = [
        "# 自动化实验数据\n",
        f"生成时间：{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n",
        "测试程序：`bmp_watermark`\n",
        "测试图片目录：`assets/test_bmps/`\n\n",
        "| 用例 | 分辨率 | 水印文本 | 位置 | 不透明度 | 耗时(ms) | 灰度min | 灰度max | 灰度均值 | 灰度标准差 | 返回码 |\n",
        "|---|---:|---|---|---:|---:|---:|---:|---:|---:|---:|\n",
    ]
    for r in rows:
        m = f"{r['mean']:.4f}" if r['mean'] is not None else "N/A"
        s = f"{r['stddev']:.4f}" if r['stddev'] is not None else "N/A"
        lines.append(
            f"| {r['case']} | {r['resolution']} | {r['text']} | {r['position']} | {r['opacity']} | "
            f"{r['elapsed_ms']} | {r['min']} | {r['max']} | {m} | {s} | {r['rc']} |\n")
    lines.append("\n说明：返回码 0 表示成功。center/tl/tr/bl/br 为单次放置，tile 为平铺水印。\n")

    with open(RESULT_MD, "w", encoding="utf-8") as f:
        f.writelines(lines)
    print(f"\nGenerated: {RESULT_MD}")


if __name__ == "__main__":
    main()
