#!/usr/bin/env python3
import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont


def collect_ppm_files(input_dir: Path):
    files = sorted(input_dir.glob("*.ppm"))
    if not files:
        raise FileNotFoundError(f"No .ppm files found in {input_dir}")
    return files


def convert_to_png(ppm_files, png_dir: Path):
    png_dir.mkdir(parents=True, exist_ok=True)
    png_files = []
    for ppm in ppm_files:
        png = png_dir / f"{ppm.stem}.png"
        with Image.open(ppm) as img:
            img.convert("RGB").save(png, format="PNG")
        png_files.append(png)
    return png_files


def make_comparison(png_files, out_file: Path, title_height: int = 42, gap: int = 16, border: int = 20):
    images = [Image.open(p).convert("RGB") for p in png_files]
    try:
        font = ImageFont.truetype("DejaVuSans.ttf", 20)
    except Exception:
        font = ImageFont.load_default()

    widths = [img.width for img in images]
    heights = [img.height for img in images]

    canvas_w = border * 2 + sum(widths) + gap * (len(images) - 1)
    canvas_h = border * 2 + title_height + max(heights)

    canvas = Image.new("RGB", (canvas_w, canvas_h), (18, 20, 24))
    draw = ImageDraw.Draw(canvas)

    x = border
    y_img = border + title_height
    for path, img in zip(png_files, images):
        canvas.paste(img, (x, y_img))
        label = path.stem
        text_bbox = draw.textbbox((0, 0), label, font=font)
        text_w = text_bbox[2] - text_bbox[0]
        draw.text((x + (img.width - text_w) // 2, border + 8), label, fill=(230, 230, 230), font=font)
        x += img.width + gap

    out_file.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(out_file, format="PNG")

    for img in images:
        img.close()


def main():
    parser = argparse.ArgumentParser(description="Convert PPM images to PNG and create a comparison sheet")
    parser.add_argument("--input-dir", default="outputs", help="Directory containing .ppm files")
    parser.add_argument("--png-dir", default="outputs/png", help="Directory to save converted PNG files")
    parser.add_argument("--comparison", default="outputs/comparison.png", help="Output comparison PNG path")
    args = parser.parse_args()

    input_dir = Path(args.input_dir).resolve()
    png_dir = Path(args.png_dir).resolve()
    comparison = Path(args.comparison).resolve()

    ppm_files = collect_ppm_files(input_dir)
    png_files = convert_to_png(ppm_files, png_dir)
    make_comparison(png_files, comparison)

    print("Converted PNG files:")
    for p in png_files:
        print(f"  {p}")
    print(f"Comparison image: {comparison}")


if __name__ == "__main__":
    main()
