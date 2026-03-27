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

using BYTE = std::uint8_t;
using WORD = std::uint16_t;
using DWORD = std::uint32_t;
using LONG = std::int32_t;

#pragma pack(push, 1)
struct BITMAPFILEHEADER {
    WORD bfType;
    DWORD bfSize;
    WORD bfReserved1;
    WORD bfReserved2;
    DWORD bfOffBits;
};

struct BITMAPINFOHEADER {
    DWORD biSize;
    LONG biWidth;
    LONG biHeight;
    WORD biPlanes;
    WORD biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG biXPelsPerMeter;
    LONG biYPelsPerMeter;
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
    int width = 0;
    int height = 0;
    std::vector<RGBQUAD> pixels;
};

struct GrayStats {
    int minGray = 255;
    int maxGray = 0;
    double meanGray = 0.0;
    double stdGray = 0.0;
};

static int rowStrideBytes(int width, int bitsPerPixel) {
    return ((width * bitsPerPixel + 31) / 32) * 4;
}

static void closeFile(FILE*& fp) {
    if (fp) {
        std::fclose(fp);
        fp = nullptr;
    }
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
    std::cout << "biBitCount: " << ih.biBitCount << "\n";
    std::cout << "biCompression: " << ih.biCompression << "\n\n";
}

static long getFileSize(FILE* fp) {
    if (std::fseek(fp, 0, SEEK_END) != 0) return -1;
    const long size = std::ftell(fp);
    if (size < 0) return -1;
    if (std::fseek(fp, 0, SEEK_SET) != 0) return -1;
    return size;
}

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
    if (infoHeader.biCompression != 0) {
        closeFile(fp); throw std::runtime_error("Only uncompressed BMP (BI_RGB) is supported.");
    }
    if (infoHeader.biBitCount != 24 && infoHeader.biBitCount != 32) {
        closeFile(fp); throw std::runtime_error("Only 24-bit and 32-bit BMP are supported.");
    }

    const long fileSize = getFileSize(fp);
    if (fileSize <= 0) { closeFile(fp); throw std::runtime_error("Failed to query file size."); }

    printHeaders(fileHeader, infoHeader);

    const int width = static_cast<int>(infoHeader.biWidth);
    const int heightAbs = static_cast<int>(infoHeader.biHeight < 0 ? -infoHeader.biHeight : infoHeader.biHeight);
    const bool topDown = (infoHeader.biHeight < 0);
    if (width <= 0 || heightAbs <= 0) { closeFile(fp); throw std::runtime_error("Invalid BMP dimensions."); }

    const int bpp = static_cast<int>(infoHeader.biBitCount);
    const int srcStride = rowStrideBytes(width, bpp);
    const long required = static_cast<long>(fileHeader.bfOffBits) + static_cast<long>(srcStride) * heightAbs;
    if (required > fileSize) { closeFile(fp); throw std::runtime_error("BMP file is truncated."); }

    if (std::fseek(fp, static_cast<long>(fileHeader.bfOffBits), SEEK_SET) != 0) {
        closeFile(fp); throw std::runtime_error("Failed to seek to pixel data.");
    }

    std::vector<BYTE> rowBuf(static_cast<std::size_t>(srcStride));
    BmpImage img;
    img.width = width;
    img.height = heightAbs;
    img.pixels.resize(static_cast<std::size_t>(width) * heightAbs);

    for (int srcY = 0; srcY < heightAbs; ++srcY) {
        if (std::fread(rowBuf.data(), 1, rowBuf.size(), fp) != rowBuf.size()) {
            closeFile(fp); throw std::runtime_error("Failed reading pixel rows.");
        }
        const int dstY = topDown ? srcY : (heightAbs - 1 - srcY);
        for (int x = 0; x < width; ++x) {
            const int off = x * (bpp / 8);
            RGBQUAD px{};
            px.b = rowBuf[off + 0];
            px.g = rowBuf[off + 1];
            px.r = rowBuf[off + 2];
            px.a = (bpp == 32) ? rowBuf[off + 3] : 255;
            img.pixels[static_cast<std::size_t>(dstY) * width + x] = px;
        }
    }
    closeFile(fp);
    return img;
}

static void writeBmp24(const std::string& outputPath, const BmpImage& img) {
    FILE* fp = std::fopen(outputPath.c_str(), "wb");
    if (!fp) throw std::runtime_error("Cannot open output file: " + outputPath);

    const int w = img.width, h = img.height;
    if (w <= 0 || h <= 0) { closeFile(fp); throw std::runtime_error("Cannot write empty image."); }

    const int dstStride = rowStrideBytes(w, 24);
    const DWORD pxSize = static_cast<DWORD>(dstStride * h);

    BITMAPFILEHEADER fh{};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fh.bfSize = fh.bfOffBits + pxSize;

    BITMAPINFOHEADER ih{};
    ih.biSize = sizeof(BITMAPINFOHEADER);
    ih.biWidth = w; ih.biHeight = h; ih.biPlanes = 1;
    ih.biBitCount = 24; ih.biCompression = 0; ih.biSizeImage = pxSize;

    if (std::fwrite(&fh, sizeof(fh), 1, fp) != 1 || std::fwrite(&ih, sizeof(ih), 1, fp) != 1) {
        closeFile(fp); throw std::runtime_error("Failed to write BMP headers.");
    }

    std::vector<BYTE> rowBuf(static_cast<std::size_t>(dstStride), 0);
    for (int y = h - 1; y >= 0; --y) {
        std::fill(rowBuf.begin(), rowBuf.end(), static_cast<BYTE>(0));
        for (int x = 0; x < w; ++x) {
            const auto& px = img.pixels[static_cast<std::size_t>(y) * w + x];
            rowBuf[x * 3 + 0] = px.b;
            rowBuf[x * 3 + 1] = px.g;
            rowBuf[x * 3 + 2] = px.r;
        }
        if (std::fwrite(rowBuf.data(), 1, rowBuf.size(), fp) != rowBuf.size()) {
            closeFile(fp); throw std::runtime_error("Failed writing pixel rows.");
        }
    }
    closeFile(fp);
}

static BYTE toGray(const RGBQUAD& px) {
    int g = static_cast<int>(0.299 * px.r + 0.587 * px.g + 0.114 * px.b + 0.5);
    return static_cast<BYTE>(std::max(0, std::min(255, g)));
}

static GrayStats calcGrayStats(const BmpImage& img) {
    GrayStats s;
    if (img.pixels.empty()) return s;
    double sum = 0, sumSq = 0;
    for (const auto& px : img.pixels) {
        double gray = 0.299 * px.r + 0.587 * px.g + 0.114 * px.b;
        int g = static_cast<int>(gray + 0.5);
        s.minGray = std::min(s.minGray, g);
        s.maxGray = std::max(s.maxGray, g);
        sum += gray; sumSq += gray * gray;
    }
    double n = static_cast<double>(img.pixels.size());
    s.meanGray = sum / n;
    s.stdGray = std::sqrt(std::max(0.0, sumSq / n - s.meanGray * s.meanGray));
    return s;
}

// ===== Enhancement Functions =====

// Mode 1: Logarithmic enhancement  s = c * log(1 + r)
static BmpImage logEnhance(const BmpImage& src) {
    BmpImage dst = src;
    const double c = 255.0 / std::log(1.0 + 255.0);
    for (auto& px : dst.pixels) {
        BYTE gray = toGray(px);
        double val = c * std::log(1.0 + static_cast<double>(gray));
        BYTE out = static_cast<BYTE>(std::max(0, std::min(255, static_cast<int>(val + 0.5))));
        px.r = px.g = px.b = out;
    }
    return dst;
}

// Mode 2: Power-law (gamma) enhancement, gamma < 1 (brighten dark areas)
static BmpImage powerLawBrighten(const BmpImage& src) {
    BmpImage dst = src;
    const double gamma = 0.4;
    const double c = 255.0 / std::pow(255.0, gamma);
    for (auto& px : dst.pixels) {
        BYTE gray = toGray(px);
        double val = c * std::pow(static_cast<double>(gray), gamma);
        BYTE out = static_cast<BYTE>(std::max(0, std::min(255, static_cast<int>(val + 0.5))));
        px.r = px.g = px.b = out;
    }
    return dst;
}

// Mode 3: Power-law (gamma) enhancement, gamma > 1 (darken bright areas)
static BmpImage powerLawDarken(const BmpImage& src) {
    BmpImage dst = src;
    const double gamma = 2.5;
    const double c = 255.0 / std::pow(255.0, gamma);
    for (auto& px : dst.pixels) {
        BYTE gray = toGray(px);
        double val = c * std::pow(static_cast<double>(gray), gamma);
        BYTE out = static_cast<BYTE>(std::max(0, std::min(255, static_cast<int>(val + 0.5))));
        px.r = px.g = px.b = out;
    }
    return dst;
}

// Mode 4: Histogram equalization
static BmpImage histogramEqualize(const BmpImage& src) {
    // Build histogram
    int hist[256] = {};
    const int total = static_cast<int>(src.pixels.size());
    for (const auto& px : src.pixels) {
        hist[toGray(px)]++;
    }

    // Build CDF
    int cdf[256] = {};
    cdf[0] = hist[0];
    for (int i = 1; i < 256; ++i) {
        cdf[i] = cdf[i - 1] + hist[i];
    }

    // Find CDF minimum (first non-zero)
    int cdfMin = 0;
    for (int i = 0; i < 256; ++i) {
        if (cdf[i] > 0) { cdfMin = cdf[i]; break; }
    }

    // Build lookup table
    BYTE lut[256];
    for (int i = 0; i < 256; ++i) {
        if (cdf[i] == 0) {
            lut[i] = 0;
        } else {
            double val = static_cast<double>(cdf[i] - cdfMin) / static_cast<double>(total - cdfMin) * 255.0;
            lut[i] = static_cast<BYTE>(std::max(0, std::min(255, static_cast<int>(val + 0.5))));
        }
    }

    BmpImage dst = src;
    for (auto& px : dst.pixels) {
        BYTE out = lut[toGray(px)];
        px.r = px.g = px.b = out;
    }
    return dst;
}

static void printUsage(const char* exe) {
    std::cout << "Usage:\n  " << exe << " <input.bmp> <mode> <output.bmp>\n";
    std::cout << "  mode: 1=Log, 2=PowerLaw(gamma=0.4), 3=PowerLaw(gamma=2.5), 4=HistEq\n";
}

int main(int argc, char* argv[]) {
    try {
        std::string inputPath, outputPath;
        int mode = 0;

        if (argc == 4) {
            inputPath = argv[1];
            mode = std::atoi(argv[2]);
            outputPath = argv[3];
        } else if (argc == 1) {
            std::cout << "Enter source BMP path: ";
            std::getline(std::cin, inputPath);
            std::cout << "Select mode (1=Log, 2=PowerLaw-bright, 3=PowerLaw-dark, 4=HistEq): ";
            std::cin >> mode;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "Enter output BMP path: ";
            std::getline(std::cin, outputPath);
        } else {
            printUsage(argv[0]);
            return 1;
        }

        if (mode < 1 || mode > 4) {
            throw std::runtime_error("Invalid mode. Choose 1-4.");
        }

        const auto t0 = std::chrono::steady_clock::now();
        const BmpImage src = readBmp(inputPath);

        BmpImage dst;
        switch (mode) {
            case 1: dst = logEnhance(src); break;
            case 2: dst = powerLawBrighten(src); break;
            case 3: dst = powerLawDarken(src); break;
            case 4: dst = histogramEqualize(src); break;
        }

        writeBmp24(outputPath, dst);
        const auto t1 = std::chrono::steady_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        const GrayStats stats = calcGrayStats(dst);

        std::cout << "Done. Output saved to: " << outputPath << "\n";
        std::cout << "Elapsed time: " << ms << " ms\n";
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
