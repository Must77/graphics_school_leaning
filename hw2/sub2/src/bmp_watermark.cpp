#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using BYTE  = std::uint8_t;
using WORD  = std::uint16_t;
using DWORD = std::uint32_t;
using LONG  = std::int32_t;

#pragma pack(push, 1)
struct BITMAPFILEHEADER {
    WORD  bfType;
    DWORD bfSize;
    WORD  bfReserved1;
    WORD  bfReserved2;
    DWORD bfOffBits;
};
struct BITMAPINFOHEADER {
    DWORD biSize;
    LONG  biWidth;
    LONG  biHeight;
    WORD  biPlanes;
    WORD  biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG  biXPelsPerMeter;
    LONG  biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
};
#pragma pack(pop)

struct RGBQUAD { BYTE b, g, r, a; };

struct BmpImage {
    int width = 0, height = 0;
    std::vector<RGBQUAD> pixels;
    RGBQUAD& at(int x, int y) { return pixels[static_cast<std::size_t>(y) * width + x]; }
    const RGBQUAD& at(int x, int y) const { return pixels[static_cast<std::size_t>(y) * width + x]; }
};

struct GrayStats { int minGray = 255, maxGray = 0; double meanGray = 0, stdGray = 0; };

/* ==================== BMP I/O ==================== */
static int rowStrideBytes(int w, int bpp) { return ((w * bpp + 31) / 32) * 4; }
static void closeFile(FILE*& fp) { if (fp) { std::fclose(fp); fp = nullptr; } }

static BmpImage readBmp(const std::string& path) {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) throw std::runtime_error("Cannot open: " + path);
    BITMAPFILEHEADER fh{}; BITMAPINFOHEADER ih{};
    if (std::fread(&fh, sizeof(fh), 1, fp) != 1) { closeFile(fp); throw std::runtime_error("Bad file header."); }
    if (std::fread(&ih, sizeof(ih), 1, fp) != 1) { closeFile(fp); throw std::runtime_error("Bad info header."); }
    if (fh.bfType != 0x4D42) { closeFile(fp); throw std::runtime_error("Not a BMP file."); }
    if (ih.biCompression != 0) { closeFile(fp); throw std::runtime_error("Only BI_RGB supported."); }
    if (ih.biBitCount != 24 && ih.biBitCount != 32) { closeFile(fp); throw std::runtime_error("Only 24/32-bit supported."); }

    const int w = static_cast<int>(ih.biWidth);
    const int h = ih.biHeight < 0 ? -static_cast<int>(ih.biHeight) : static_cast<int>(ih.biHeight);
    const bool topDown = (ih.biHeight < 0);
    if (w <= 0 || h <= 0) { closeFile(fp); throw std::runtime_error("Invalid dimensions."); }

    const int bpp = static_cast<int>(ih.biBitCount);
    const int stride = rowStrideBytes(w, bpp);

    if (std::fseek(fp, 0, SEEK_END) != 0) { closeFile(fp); throw std::runtime_error("Seek failed."); }
    const long fsz = std::ftell(fp);
    if (static_cast<long>(fh.bfOffBits) + static_cast<long>(stride) * h > fsz) {
        closeFile(fp); throw std::runtime_error("File truncated.");
    }
    if (std::fseek(fp, static_cast<long>(fh.bfOffBits), SEEK_SET) != 0) {
        closeFile(fp); throw std::runtime_error("Seek to pixels failed.");
    }

    std::cout << "[BMP] " << w << "x" << h << " " << bpp << "-bit\n";

    BmpImage img; img.width = w; img.height = h;
    img.pixels.resize(static_cast<std::size_t>(w) * h);
    std::vector<BYTE> row(static_cast<std::size_t>(stride));
    for (int sy = 0; sy < h; ++sy) {
        if (std::fread(row.data(), 1, row.size(), fp) != row.size()) {
            closeFile(fp); throw std::runtime_error("Read pixel row failed.");
        }
        const int dy = topDown ? sy : (h - 1 - sy);
        for (int x = 0; x < w; ++x) {
            const int off = x * (bpp / 8);
            RGBQUAD px{};
            px.b = row[off]; px.g = row[off + 1]; px.r = row[off + 2];
            px.a = (bpp == 32) ? row[off + 3] : 255;
            img.at(x, dy) = px;
        }
    }
    closeFile(fp);
    return img;
}

static void writeBmp24(const std::string& path, const BmpImage& img) {
    FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) throw std::runtime_error("Cannot open output: " + path);
    const int w = img.width, h = img.height;
    const int stride = rowStrideBytes(w, 24);
    const DWORD pds = static_cast<DWORD>(stride * h);
    BITMAPFILEHEADER fh{}; fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(BITMAPINFOHEADER); fh.bfSize = fh.bfOffBits + pds;
    BITMAPINFOHEADER ih{}; ih.biSize = sizeof(ih); ih.biWidth = w; ih.biHeight = h;
    ih.biPlanes = 1; ih.biBitCount = 24; ih.biSizeImage = pds;
    if (std::fwrite(&fh, sizeof(fh), 1, fp) != 1 || std::fwrite(&ih, sizeof(ih), 1, fp) != 1) {
        closeFile(fp); throw std::runtime_error("Write header failed.");
    }
    std::vector<BYTE> row(static_cast<std::size_t>(stride), 0);
    for (int y = h - 1; y >= 0; --y) {
        std::fill(row.begin(), row.end(), BYTE(0));
        for (int x = 0; x < w; ++x) {
            const auto& px = img.at(x, y);
            row[x * 3] = px.b; row[x * 3 + 1] = px.g; row[x * 3 + 2] = px.r;
        }
        if (std::fwrite(row.data(), 1, row.size(), fp) != row.size()) {
            closeFile(fp); throw std::runtime_error("Write row failed.");
        }
    }
    closeFile(fp);
}

/* ==================== Built-in 8x8 bitmap font ==================== */
static const BYTE FONT_8X8[][8] = {
    // 0x20 SPACE
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // 0x21 !
    {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00},
    // 0x22 "
    {0x6C,0x6C,0x24,0x00,0x00,0x00,0x00,0x00},
    // 0x23 #
    {0x6C,0xFE,0x6C,0x6C,0xFE,0x6C,0x00,0x00},
    // 0x24 $
    {0x18,0x7E,0xC0,0x7C,0x06,0xFC,0x18,0x00},
    // 0x25 %
    {0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00,0x00},
    // 0x26 &
    {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00},
    // 0x27 '
    {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00},
    // 0x28 (
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},
    // 0x29 )
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},
    // 0x2A *
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
    // 0x2B +
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
    // 0x2C ,
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30},
    // 0x2D -
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    // 0x2E .
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    // 0x2F /
    {0x06,0x0C,0x18,0x30,0x60,0xC0,0x00,0x00},
    // 0x30 0
    {0x7C,0xC6,0xCE,0xD6,0xE6,0xC6,0x7C,0x00},
    // 0x31 1
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
    // 0x32 2
    {0x7C,0xC6,0x06,0x1C,0x30,0x60,0xFE,0x00},
    // 0x33 3
    {0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00},
    // 0x34 4
    {0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x00},
    // 0x35 5
    {0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00},
    // 0x36 6
    {0x3C,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00},
    // 0x37 7
    {0xFE,0x06,0x0C,0x18,0x30,0x30,0x30,0x00},
    // 0x38 8
    {0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00},
    // 0x39 9
    {0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00},
    // 0x3A :
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00},
    // 0x3B ;
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30},
    // 0x3C <
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00},
    // 0x3D =
    {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00},
    // 0x3E >
    {0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00},
    // 0x3F ?
    {0x7C,0xC6,0x0C,0x18,0x18,0x00,0x18,0x00},
    // 0x40 @
    {0x7C,0xC6,0xDE,0xDE,0xDC,0xC0,0x7C,0x00},
    // 0x41 A
    {0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0x00},
    // 0x42 B
    {0xFC,0xC6,0xC6,0xFC,0xC6,0xC6,0xFC,0x00},
    // 0x43 C
    {0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x00},
    // 0x44 D
    {0xF8,0xCC,0xC6,0xC6,0xC6,0xCC,0xF8,0x00},
    // 0x45 E
    {0xFE,0xC0,0xC0,0xFC,0xC0,0xC0,0xFE,0x00},
    // 0x46 F
    {0xFE,0xC0,0xC0,0xFC,0xC0,0xC0,0xC0,0x00},
    // 0x47 G
    {0x7C,0xC6,0xC0,0xCE,0xC6,0xC6,0x7E,0x00},
    // 0x48 H
    {0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00},
    // 0x49 I
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x00},
    // 0x4A J
    {0x1E,0x06,0x06,0x06,0xC6,0xC6,0x7C,0x00},
    // 0x4B K
    {0xC6,0xCC,0xD8,0xF0,0xD8,0xCC,0xC6,0x00},
    // 0x4C L
    {0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xFE,0x00},
    // 0x4D M
    {0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00},
    // 0x4E N
    {0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00},
    // 0x4F O
    {0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00},
    // 0x50 P
    {0xFC,0xC6,0xC6,0xFC,0xC0,0xC0,0xC0,0x00},
    // 0x51 Q
    {0x7C,0xC6,0xC6,0xC6,0xD6,0xCC,0x76,0x00},
    // 0x52 R
    {0xFC,0xC6,0xC6,0xFC,0xD8,0xCC,0xC6,0x00},
    // 0x53 S
    {0x7C,0xC6,0xC0,0x7C,0x06,0xC6,0x7C,0x00},
    // 0x54 T
    {0xFE,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
    // 0x55 U
    {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00},
    // 0x56 V
    {0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00},
    // 0x57 W
    {0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00},
    // 0x58 X
    {0xC6,0x6C,0x38,0x38,0x6C,0xC6,0xC6,0x00},
    // 0x59 Y
    {0xC6,0xC6,0x6C,0x38,0x18,0x18,0x18,0x00},
    // 0x5A Z
    {0xFE,0x06,0x0C,0x18,0x30,0x60,0xFE,0x00},
    // 0x5B [
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00},
    // 0x5C backslash
    {0xC0,0x60,0x30,0x18,0x0C,0x06,0x00,0x00},
    // 0x5D ]
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00},
    // 0x5E ^
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00},
    // 0x5F _
    {0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0x00},
    // 0x60 `
    {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00},
    // 0x61 a
    {0x00,0x00,0x7C,0x06,0x7E,0xC6,0x7E,0x00},
    // 0x62 b
    {0xC0,0xC0,0xFC,0xC6,0xC6,0xC6,0xFC,0x00},
    // 0x63 c
    {0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00},
    // 0x64 d
    {0x06,0x06,0x7E,0xC6,0xC6,0xC6,0x7E,0x00},
    // 0x65 e
    {0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00},
    // 0x66 f
    {0x1C,0x36,0x30,0x7C,0x30,0x30,0x30,0x00},
    // 0x67 g
    {0x00,0x00,0x7E,0xC6,0xC6,0x7E,0x06,0x7C},
    // 0x68 h
    {0xC0,0xC0,0xFC,0xC6,0xC6,0xC6,0xC6,0x00},
    // 0x69 i
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},
    // 0x6A j
    {0x06,0x00,0x0E,0x06,0x06,0x06,0xC6,0x7C},
    // 0x6B k
    {0xC0,0xC0,0xCC,0xD8,0xF0,0xD8,0xCC,0x00},
    // 0x6C l
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    // 0x6D m
    {0x00,0x00,0xCC,0xFE,0xD6,0xC6,0xC6,0x00},
    // 0x6E n
    {0x00,0x00,0xFC,0xC6,0xC6,0xC6,0xC6,0x00},
    // 0x6F o
    {0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00},
    // 0x70 p
    {0x00,0x00,0xFC,0xC6,0xC6,0xFC,0xC0,0xC0},
    // 0x71 q
    {0x00,0x00,0x7E,0xC6,0xC6,0x7E,0x06,0x06},
    // 0x72 r
    {0x00,0x00,0xDC,0xE6,0xC0,0xC0,0xC0,0x00},
    // 0x73 s
    {0x00,0x00,0x7E,0xC0,0x7C,0x06,0xFC,0x00},
    // 0x74 t
    {0x30,0x30,0x7C,0x30,0x30,0x36,0x1C,0x00},
    // 0x75 u
    {0x00,0x00,0xC6,0xC6,0xC6,0xC6,0x7E,0x00},
    // 0x76 v
    {0x00,0x00,0xC6,0xC6,0x6C,0x38,0x10,0x00},
    // 0x77 w
    {0x00,0x00,0xC6,0xC6,0xD6,0xFE,0x6C,0x00},
    // 0x78 x
    {0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00},
    // 0x79 y
    {0x00,0x00,0xC6,0xC6,0xC6,0x7E,0x06,0x7C},
    // 0x7A z
    {0x00,0x00,0xFE,0x0C,0x38,0x60,0xFE,0x00},
};
static const int FONT_FIRST = 0x20;
static const int FONT_LAST  = 0x7A;

static const BYTE* getGlyph(char ch) {
    int idx = static_cast<unsigned char>(ch) - FONT_FIRST;
    if (idx < 0 || idx > FONT_LAST - FONT_FIRST) return FONT_8X8[0]; // space
    return FONT_8X8[idx];
}

/* ==================== Alpha-blended text rendering ==================== */
static RGBQUAD alphaBlend(RGBQUAD bg, RGBQUAD fg, double alpha) {
    auto mix = [](BYTE a, BYTE b, double t) -> BYTE {
        return static_cast<BYTE>(std::min(255.0, std::max(0.0, a * (1.0 - t) + b * t + 0.5)));
    };
    return { mix(bg.b, fg.b, alpha), mix(bg.g, fg.g, alpha), mix(bg.r, fg.r, alpha), 255 };
}

static void renderText(BmpImage& img, const std::string& text,
                       int startX, int startY, int scale,
                       RGBQUAD color, double opacity) {
    const int charW = 8 * scale;
    int cx = startX;
    for (char ch : text) {
        const BYTE* glyph = getGlyph(ch);
        for (int row = 0; row < 8; ++row) {
            BYTE bits = glyph[row];
            for (int col = 0; col < 8; ++col) {
                if (bits & (0x80 >> col)) {
                    for (int sy = 0; sy < scale; ++sy) {
                        for (int sx = 0; sx < scale; ++sx) {
                            int px = cx + col * scale + sx;
                            int py = startY + row * scale + sy;
                            if (px >= 0 && px < img.width && py >= 0 && py < img.height) {
                                img.at(px, py) = alphaBlend(img.at(px, py), color, opacity);
                            }
                        }
                    }
                }
            }
        }
        cx += charW;
    }
}

static int textPixelWidth(const std::string& text, int scale) {
    return static_cast<int>(text.size()) * 8 * scale;
}

/* ==================== Watermark placement ==================== */

// position: "center", "tl", "tr", "bl", "br", "tile"
static BmpImage addWatermark(const BmpImage& src, const std::string& text,
                             double opacity, const std::string& position) {
    BmpImage dst = src;

    int scale = std::max(1, std::min(dst.width, dst.height) / 128);
    const int charH = 8 * scale;
    const int tw = textPixelWidth(text, scale);
    const int margin = scale * 4;

    RGBQUAD white = {255, 255, 255, 255};

    if (position == "tile") {
        int spacingX = tw + margin * 4;
        int spacingY = charH + margin * 8;
        if (spacingX <= 0) spacingX = 1;
        if (spacingY <= 0) spacingY = 1;
        for (int y = margin; y < dst.height; y += spacingY) {
            for (int x = -tw / 2 + (((y / spacingY) % 2) * spacingX / 2); x < dst.width; x += spacingX) {
                renderText(dst, text, x, y, scale, white, opacity);
            }
        }
    } else {
        int x = 0, y = 0;
        if (position == "tl") {
            x = margin; y = margin;
        } else if (position == "tr") {
            x = dst.width - tw - margin; y = margin;
        } else if (position == "bl") {
            x = margin; y = dst.height - charH - margin;
        } else if (position == "br") {
            x = dst.width - tw - margin; y = dst.height - charH - margin;
        } else { // center
            x = (dst.width - tw) / 2; y = (dst.height - charH) / 2;
        }
        renderText(dst, text, x, y, scale, white, opacity);
    }

    return dst;
}

/* ==================== Gray statistics ==================== */
static GrayStats calcGrayStats(const BmpImage& img) {
    GrayStats s;
    if (img.pixels.empty()) return s;
    double sum = 0, sumSq = 0;
    for (const auto& px : img.pixels) {
        double g = 0.299 * px.r + 0.587 * px.g + 0.114 * px.b;
        int gi = static_cast<int>(g + 0.5);
        s.minGray = std::min(s.minGray, gi);
        s.maxGray = std::max(s.maxGray, gi);
        sum += g; sumSq += g * g;
    }
    double n = static_cast<double>(img.pixels.size());
    s.meanGray = sum / n;
    s.stdGray = std::sqrt(std::max(0.0, sumSq / n - s.meanGray * s.meanGray));
    return s;
}

/* ==================== Main ==================== */
static void printUsage(const char* exe) {
    std::cout << "Usage:\n  " << exe
              << " <input.bmp> <text> <output.bmp> [opacity=0.3] [position]\n"
              << "  position: center (default), tl, tr, bl, br, tile\n";
}

int main(int argc, char* argv[]) {
    try {
        if (argc < 4 || argc > 6) {
            printUsage(argv[0]);
            return 1;
        }

        std::string inputPath  = argv[1];
        std::string text       = argv[2];
        std::string outputPath = argv[3];
        double opacity = 0.3;
        std::string position = "center";

        if (argc >= 5) opacity  = std::atof(argv[4]);
        if (argc >= 6) position = argv[5];

        if (opacity < 0.0) opacity = 0.0;
        if (opacity > 1.0) opacity = 1.0;

        std::cout << "Watermark text: \"" << text << "\"\n";
        std::cout << "Opacity: " << opacity << "  Position: " << position << "\n";

        const auto t0 = std::chrono::steady_clock::now();

        BmpImage src = readBmp(inputPath);
        BmpImage dst = addWatermark(src, text, opacity, position);
        writeBmp24(outputPath, dst);

        const auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        GrayStats st = calcGrayStats(dst);

        std::cout << "Done. Output saved to: " << outputPath << "\n";
        std::cout << "Elapsed time: " << ms << " ms\n";
        std::cout << "Output gray statistics: min=" << st.minGray
                  << ", max=" << st.maxGray
                  << ", mean=" << st.meanGray
                  << ", stddev=" << st.stdGray << "\n";
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
