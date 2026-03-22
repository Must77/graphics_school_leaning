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

struct RGBQUAD {
    BYTE b;
    BYTE g;
    BYTE r;
    BYTE a;
};

struct BmpImage {
    int width  = 0;
    int height = 0;
    std::vector<RGBQUAD> pixels;

    RGBQUAD getPixel(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) {
            return RGBQUAD{255, 255, 255, 255};
        }
        return pixels[static_cast<std::size_t>(y) * width + x];
    }
};

struct GrayStats {
    int    minGray  = 255;
    int    maxGray  = 0;
    double meanGray = 0.0;
    double stdGray  = 0.0;
};

static const double PI = 3.14159265358979323846;

static int rowStrideBytes(int width, int bitsPerPixel) {
    return ((width * bitsPerPixel + 31) / 32) * 4;
}

static void closeFile(FILE*& fp) {
    if (fp) { std::fclose(fp); fp = nullptr; }
}

static void printUsage(const std::string& exeName) {
    std::cout << "Usage:\n";
    std::cout << "  " << exeName << " <input.bmp> <angle_degrees> <output.bmp>\n";
    std::cout << "  angle: rotation angle in degrees (clockwise), e.g. 90, 180, 270, 45.5\n\n";
    std::cout << "Or run without parameters to use interactive mode.\n";
}

static void printHeaders(const BITMAPFILEHEADER& fh, const BITMAPINFOHEADER& ih) {
    std::cout << "[BMP FILE HEADER]\n";
    std::cout << "bfType: 0x" << std::hex << fh.bfType << std::dec << "\n";
    std::cout << "bfSize: " << fh.bfSize << "\n";
    std::cout << "bfOffBits: " << fh.bfOffBits << "\n\n";

    std::cout << "[BMP INFO HEADER]\n";
    std::cout << "biSize: " << ih.biSize << "\n";
    std::cout << "biWidth: " << ih.biWidth << "\n";
    std::cout << "biHeight: " << ih.biHeight << "\n";
    std::cout << "biPlanes: " << ih.biPlanes << "\n";
    std::cout << "biBitCount: " << ih.biBitCount << "\n";
    std::cout << "biCompression: " << ih.biCompression << "\n";
    std::cout << "biSizeImage: " << ih.biSizeImage << "\n\n";

    if (ih.biBitCount == 24) {
        std::cout << "Pixel format: 24-bit BGR, 3 bytes per pixel.\n";
    } else if (ih.biBitCount == 32) {
        std::cout << "Pixel format: 32-bit BGRA, 4 bytes per pixel.\n";
    }
}

static long getFileSize(FILE* fp) {
    if (std::fseek(fp, 0, SEEK_END) != 0) return -1;
    const long size = std::ftell(fp);
    if (size < 0) return -1;
    if (std::fseek(fp, 0, SEEK_SET) != 0) return -1;
    return size;
}

/* ==================== BMP Read ==================== */
static BmpImage readBmp(const std::string& inputPath) {
    FILE* fp = std::fopen(inputPath.c_str(), "rb");
    if (!fp) throw std::runtime_error("Cannot open input BMP file: " + inputPath);

    BITMAPFILEHEADER fileHeader{};
    BITMAPINFOHEADER infoHeader{};

    if (std::fread(&fileHeader, sizeof(fileHeader), 1, fp) != 1) {
        closeFile(fp); throw std::runtime_error("Failed to read BITMAPFILEHEADER.");
    }
    if (std::fread(&infoHeader, sizeof(infoHeader), 1, fp) != 1) {
        closeFile(fp); throw std::runtime_error("Failed to read BITMAPINFOHEADER.");
    }
    if (fileHeader.bfType != 0x4D42) {
        closeFile(fp); throw std::runtime_error("Input file is not a BMP file.");
    }
    if (infoHeader.biSize < sizeof(BITMAPINFOHEADER)) {
        closeFile(fp); throw std::runtime_error("Unsupported BMP info header size.");
    }
    if (infoHeader.biPlanes != 1) {
        closeFile(fp); throw std::runtime_error("Invalid BMP planes value (must be 1).");
    }
    if (infoHeader.biCompression != 0) {
        closeFile(fp); throw std::runtime_error("Only uncompressed BMP (BI_RGB) is supported.");
    }
    if (infoHeader.biBitCount != 24 && infoHeader.biBitCount != 32) {
        closeFile(fp); throw std::runtime_error("Only 24-bit and 32-bit BMP are supported.");
    }

    const long fileSize = getFileSize(fp);
    if (fileSize <= 0) {
        closeFile(fp); throw std::runtime_error("Failed to query input file size.");
    }

    printHeaders(fileHeader, infoHeader);

    const int width    = static_cast<int>(infoHeader.biWidth);
    const int heightAbs = static_cast<int>(infoHeader.biHeight < 0 ? -infoHeader.biHeight : infoHeader.biHeight);
    const bool topDown  = (infoHeader.biHeight < 0);

    if (width <= 0 || heightAbs <= 0) {
        closeFile(fp); throw std::runtime_error("Invalid BMP image dimensions.");
    }

    const int bitsPerPixel = static_cast<int>(infoHeader.biBitCount);
    const int srcStride    = rowStrideBytes(width, bitsPerPixel);
    const long requiredBytes = static_cast<long>(fileHeader.bfOffBits) + static_cast<long>(srcStride) * heightAbs;
    if (requiredBytes > fileSize) {
        closeFile(fp); throw std::runtime_error("BMP file is truncated: pixel data exceeds file size.");
    }
    if (fileHeader.bfOffBits < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)) {
        closeFile(fp); throw std::runtime_error("Invalid pixel data offset (bfOffBits).");
    }
    if (std::fseek(fp, static_cast<long>(fileHeader.bfOffBits), SEEK_SET) != 0) {
        closeFile(fp); throw std::runtime_error("Failed to seek to pixel data.");
    }

    std::vector<BYTE> rowBuffer(static_cast<std::size_t>(srcStride));
    BmpImage image;
    image.width  = width;
    image.height = heightAbs;
    image.pixels.resize(static_cast<std::size_t>(width) * heightAbs);

    for (int srcY = 0; srcY < heightAbs; ++srcY) {
        if (std::fread(rowBuffer.data(), 1, static_cast<std::size_t>(srcStride), fp) != static_cast<std::size_t>(srcStride)) {
            closeFile(fp); throw std::runtime_error("Failed while reading BMP pixel rows.");
        }
        const int dstY = topDown ? srcY : (heightAbs - 1 - srcY);
        for (int x = 0; x < width; ++x) {
            const int srcOffset = x * (bitsPerPixel / 8);
            RGBQUAD px{};
            px.b = rowBuffer[static_cast<std::size_t>(srcOffset) + 0];
            px.g = rowBuffer[static_cast<std::size_t>(srcOffset) + 1];
            px.r = rowBuffer[static_cast<std::size_t>(srcOffset) + 2];
            px.a = (bitsPerPixel == 32) ? rowBuffer[static_cast<std::size_t>(srcOffset) + 3] : static_cast<BYTE>(255);
            image.pixels[static_cast<std::size_t>(dstY) * width + x] = px;
        }
    }

    closeFile(fp);
    return image;
}

/* ==================== BMP Write (24-bit) ==================== */
static void writeBmp24(const std::string& outputPath, const BmpImage& image) {
    FILE* fp = std::fopen(outputPath.c_str(), "wb");
    if (!fp) throw std::runtime_error("Cannot open output BMP file: " + outputPath);

    const int width  = image.width;
    const int height = image.height;
    if (width <= 0 || height <= 0) {
        closeFile(fp); throw std::runtime_error("Cannot write empty image.");
    }

    const int dstStride       = rowStrideBytes(width, 24);
    const DWORD pixelDataSize = static_cast<DWORD>(dstStride * height);

    BITMAPFILEHEADER fileHeader{};
    fileHeader.bfType    = 0x4D42;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize    = fileHeader.bfOffBits + pixelDataSize;

    BITMAPINFOHEADER infoHeader{};
    infoHeader.biSize        = sizeof(BITMAPINFOHEADER);
    infoHeader.biWidth       = width;
    infoHeader.biHeight      = height;
    infoHeader.biPlanes      = 1;
    infoHeader.biBitCount    = 24;
    infoHeader.biCompression = 0;
    infoHeader.biSizeImage   = pixelDataSize;

    if (std::fwrite(&fileHeader, sizeof(fileHeader), 1, fp) != 1 ||
        std::fwrite(&infoHeader, sizeof(infoHeader), 1, fp) != 1) {
        closeFile(fp); throw std::runtime_error("Failed to write BMP headers.");
    }

    std::vector<BYTE> rowBuffer(static_cast<std::size_t>(dstStride), static_cast<BYTE>(0));

    for (int y = height - 1; y >= 0; --y) {
        std::fill(rowBuffer.begin(), rowBuffer.end(), static_cast<BYTE>(0));
        for (int x = 0; x < width; ++x) {
            const RGBQUAD& px = image.pixels[static_cast<std::size_t>(y) * width + x];
            const int offset = x * 3;
            rowBuffer[static_cast<std::size_t>(offset) + 0] = px.b;
            rowBuffer[static_cast<std::size_t>(offset) + 1] = px.g;
            rowBuffer[static_cast<std::size_t>(offset) + 2] = px.r;
        }
        if (std::fwrite(rowBuffer.data(), 1, rowBuffer.size(), fp) != rowBuffer.size()) {
            closeFile(fp); throw std::runtime_error("Failed while writing BMP pixel rows.");
        }
    }

    closeFile(fp);
}

/* ==================== Rotation: 90 CW ==================== */
static BmpImage rotate90(const BmpImage& src) {
    BmpImage dst;
    dst.width  = src.height;
    dst.height = src.width;
    dst.pixels.resize(static_cast<std::size_t>(dst.width) * dst.height);

    for (int y = 0; y < src.height; ++y) {
        for (int x = 0; x < src.width; ++x) {
            const int nx = src.height - 1 - y;
            const int ny = x;
            dst.pixels[static_cast<std::size_t>(ny) * dst.width + nx] =
                src.pixels[static_cast<std::size_t>(y) * src.width + x];
        }
    }
    return dst;
}

/* ==================== Rotation: 180 ==================== */
static BmpImage rotate180(const BmpImage& src) {
    BmpImage dst;
    dst.width  = src.width;
    dst.height = src.height;
    dst.pixels.resize(static_cast<std::size_t>(dst.width) * dst.height);

    for (int y = 0; y < src.height; ++y) {
        for (int x = 0; x < src.width; ++x) {
            const int nx = src.width  - 1 - x;
            const int ny = src.height - 1 - y;
            dst.pixels[static_cast<std::size_t>(ny) * dst.width + nx] =
                src.pixels[static_cast<std::size_t>(y) * src.width + x];
        }
    }
    return dst;
}

/* ==================== Rotation: 270 CW (90 CCW) ==================== */
static BmpImage rotate270(const BmpImage& src) {
    BmpImage dst;
    dst.width  = src.height;
    dst.height = src.width;
    dst.pixels.resize(static_cast<std::size_t>(dst.width) * dst.height);

    for (int y = 0; y < src.height; ++y) {
        for (int x = 0; x < src.width; ++x) {
            const int nx = y;
            const int ny = src.width - 1 - x;
            dst.pixels[static_cast<std::size_t>(ny) * dst.width + nx] =
                src.pixels[static_cast<std::size_t>(y) * src.width + x];
        }
    }
    return dst;
}

/* ==================== Bilinear interpolation helper ==================== */
static RGBQUAD bilinearSample(const BmpImage& src, double sx, double sy) {
    const int x0 = static_cast<int>(std::floor(sx));
    const int y0 = static_cast<int>(std::floor(sy));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;

    const double fx = sx - x0;
    const double fy = sy - y0;

    const RGBQUAD p00 = src.getPixel(x0, y0);
    const RGBQUAD p10 = src.getPixel(x1, y0);
    const RGBQUAD p01 = src.getPixel(x0, y1);
    const RGBQUAD p11 = src.getPixel(x1, y1);

    auto lerp = [](double a, double b, double t) -> BYTE {
        double v = a * (1.0 - t) + b * t;
        int iv = static_cast<int>(v + 0.5);
        return static_cast<BYTE>(iv < 0 ? 0 : (iv > 255 ? 255 : iv));
    };

    RGBQUAD result{};
    double r_top = p00.r * (1.0 - fx) + p10.r * fx;
    double r_bot = p01.r * (1.0 - fx) + p11.r * fx;
    result.r = lerp(r_top, r_bot, fy);

    double g_top = p00.g * (1.0 - fx) + p10.g * fx;
    double g_bot = p01.g * (1.0 - fx) + p11.g * fx;
    result.g = lerp(g_top, g_bot, fy);

    double b_top = p00.b * (1.0 - fx) + p10.b * fx;
    double b_bot = p01.b * (1.0 - fx) + p11.b * fx;
    result.b = lerp(b_top, b_bot, fy);

    result.a = 255;
    return result;
}

/* ==================== Arbitrary angle rotation ==================== */
static BmpImage rotateArbitrary(const BmpImage& src, double angleDeg) {
    const double rad = angleDeg * PI / 180.0;
    const double cosA = std::cos(rad);
    const double sinA = std::sin(rad);

    const double cx = src.width  / 2.0;
    const double cy = src.height / 2.0;

    double cornersX[4], cornersY[4];
    double dx[4] = { -cx,  src.width - cx,  -cx,              src.width - cx };
    double dy[4] = { -cy,  -cy,              src.height - cy,  src.height - cy };
    for (int i = 0; i < 4; ++i) {
        cornersX[i] = dx[i] * cosA - dy[i] * sinA;
        cornersY[i] = dx[i] * sinA + dy[i] * cosA;
    }

    double minX = cornersX[0], maxX = cornersX[0];
    double minY = cornersY[0], maxY = cornersY[0];
    for (int i = 1; i < 4; ++i) {
        minX = std::min(minX, cornersX[i]);
        maxX = std::max(maxX, cornersX[i]);
        minY = std::min(minY, cornersY[i]);
        maxY = std::max(maxY, cornersY[i]);
    }

    const int dstW = static_cast<int>(std::ceil(maxX - minX));
    const int dstH = static_cast<int>(std::ceil(maxY - minY));

    if (dstW <= 0 || dstH <= 0) {
        throw std::runtime_error("Computed destination image size is invalid.");
    }

    const double dstCx = dstW / 2.0;
    const double dstCy = dstH / 2.0;

    BmpImage dst;
    dst.width  = dstW;
    dst.height = dstH;
    dst.pixels.resize(static_cast<std::size_t>(dstW) * dstH, RGBQUAD{255, 255, 255, 255});

    for (int dy = 0; dy < dstH; ++dy) {
        for (int ddx = 0; ddx < dstW; ++ddx) {
            const double rx = ddx - dstCx;
            const double ry = dy  - dstCy;

            const double sx = rx * cosA + ry * sinA + cx;
            const double sy = -rx * sinA + ry * cosA + cy;

            if (sx >= -0.5 && sx <= src.width - 0.5 && sy >= -0.5 && sy <= src.height - 0.5) {
                dst.pixels[static_cast<std::size_t>(dy) * dstW + ddx] = bilinearSample(src, sx, sy);
            }
        }
    }

    return dst;
}

/* ==================== Unified rotation dispatcher ==================== */
static BmpImage rotateBmp(const BmpImage& src, double angleDeg) {
    double norm = std::fmod(angleDeg, 360.0);
    if (norm < 0) norm += 360.0;

    if (std::fabs(norm - 90.0) < 0.01) {
        std::cout << "Using fast 90-degree rotation.\n";
        return rotate90(src);
    }
    if (std::fabs(norm - 180.0) < 0.01) {
        std::cout << "Using fast 180-degree rotation.\n";
        return rotate180(src);
    }
    if (std::fabs(norm - 270.0) < 0.01) {
        std::cout << "Using fast 270-degree rotation.\n";
        return rotate270(src);
    }
    if (std::fabs(norm) < 0.01 || std::fabs(norm - 360.0) < 0.01) {
        std::cout << "Angle is 0 degrees, returning copy.\n";
        return src;
    }

    std::cout << "Using bilinear interpolation rotation at " << angleDeg << " degrees.\n";
    return rotateArbitrary(src, angleDeg);
}

/* ==================== Gray statistics ==================== */
static GrayStats calcGrayStats(const BmpImage& image) {
    GrayStats s;
    if (image.pixels.empty()) return s;

    double sum = 0.0, sumSq = 0.0;
    for (const auto& px : image.pixels) {
        const double gray = 0.299 * px.r + 0.587 * px.g + 0.114 * px.b;
        const int g = static_cast<int>(gray + 0.5);
        s.minGray = std::min(s.minGray, g);
        s.maxGray = std::max(s.maxGray, g);
        sum += gray;
        sumSq += gray * gray;
    }
    const double n = static_cast<double>(image.pixels.size());
    s.meanGray = sum / n;
    const double variance = std::max(0.0, (sumSq / n) - (s.meanGray * s.meanGray));
    s.stdGray = std::sqrt(variance);
    return s;
}

/* ==================== Argument parsing ==================== */
static void parseArgsOrInteractive(
    int argc, char* argv[],
    std::string& inputPath, std::string& outputPath, double& angle)
{
    if (argc == 4) {
        inputPath  = argv[1];
        angle      = std::atof(argv[2]);
        outputPath = argv[3];
        return;
    }
    if (argc != 1) {
        printUsage(argv[0]);
        throw std::runtime_error("Invalid arguments. Expected 3 arguments or interactive mode.");
    }

    std::cout << "Enter source BMP path: ";
    std::getline(std::cin, inputPath);
    if (inputPath.empty()) throw std::runtime_error("Input path is empty.");

    std::cout << "Enter rotation angle (degrees, clockwise): ";
    std::cin >> angle;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    std::cout << "Enter destination BMP path: ";
    std::getline(std::cin, outputPath);
    if (outputPath.empty()) throw std::runtime_error("Output path is empty.");
}

/* ==================== Main ==================== */
int main(int argc, char* argv[]) {
    try {
        std::string inputPath, outputPath;
        double angle = 0.0;

        parseArgsOrInteractive(argc, argv, inputPath, outputPath, angle);

        const auto t0 = std::chrono::steady_clock::now();

        const BmpImage src = readBmp(inputPath);
        std::cout << "Source image: " << src.width << " x " << src.height << "\n";

        const BmpImage dst = rotateBmp(src, angle);
        std::cout << "Output image: " << dst.width << " x " << dst.height << "\n";

        writeBmp24(outputPath, dst);

        const auto t1 = std::chrono::steady_clock::now();
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        const GrayStats stats = calcGrayStats(dst);

        std::cout << "Done. Output saved to: " << outputPath << "\n";
        std::cout << "Elapsed time: " << elapsedMs << " ms\n";
        std::cout << "Output gray statistics: min=" << stats.minGray
                  << ", max=" << stats.maxGray
                  << ", mean=" << stats.meanGray
                  << ", stddev=" << stats.stdGray << "\n";

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
