#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys
from pathlib import Path


def read_ppm(path: Path):
    with path.open("rb") as f:
        magic = f.readline().strip()
        if magic != b"P6":
            raise ValueError(f"{path} is not P6 PPM")

        line = f.readline().strip()
        while line.startswith(b"#"):
            line = f.readline().strip()

        parts = line.split()
        if len(parts) != 2:
            raise ValueError(f"Invalid PPM size header in {path}")
        width, height = int(parts[0]), int(parts[1])

        maxv = int(f.readline().strip())
        if maxv != 255:
            raise ValueError(f"Unsupported max value {maxv} in {path}")

        data = f.read()
        if len(data) != width * height * 3:
            raise ValueError(f"PPM data length mismatch in {path}")

    return width, height, data


def image_metrics(data: bytes):
    pixels = len(data) // 3
    unique = len(set(data[i : i + 3] for i in range(0, len(data), 3)))
    total = sum(data)
    avg = total / (pixels * 3)
    return unique, avg


def run_one(exe: Path, out: Path, width: int, height: int, preset: int):
    cmd = [
        str(exe),
        "--headless",
        "--output",
        str(out),
        "--width",
        str(width),
        "--height",
        str(height),
        "--preset",
        str(preset),
    ]
    subprocess.run(cmd, check=True)


def main():
    parser = argparse.ArgumentParser(description="Batch-generate perspective distortion images and validate them")
    parser.add_argument("--exe", default="./build/perspective_distortion", help="Path to executable")
    parser.add_argument("--out-dir", default="outputs", help="Output image directory")
    parser.add_argument("--width", type=int, default=960)
    parser.add_argument("--height", type=int, default=720)
    args = parser.parse_args()

    exe = Path(args.exe).resolve()
    if not exe.exists():
        print(f"Executable not found: {exe}", file=sys.stderr)
        return 1

    out_dir = Path(args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    summaries = []
    for preset in [0, 1, 2]:
        out = out_dir / f"distortion_preset_{preset}.ppm"
        run_one(exe, out, args.width, args.height, preset)

        w, h, data = read_ppm(out)
        unique, avg = image_metrics(data)

        # Basic sanity checks: non-trivial content and expected size.
        if w != args.width or h != args.height:
            raise RuntimeError(f"Unexpected image size for {out}: {w}x{h}")
        if unique < 100:
            raise RuntimeError(f"Image seems too simple for {out}, unique colors={unique}")
        if avg <= 1.0 or avg >= 254.0:
            raise RuntimeError(f"Image average intensity out of range for {out}, avg={avg:.2f}")

        summaries.append((out, unique, avg))

    print("Generated and validated images:")
    for out, unique, avg in summaries:
        print(f"  {out} | unique_colors={unique} | avg_intensity={avg:.2f}")

    print("Auto test PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
