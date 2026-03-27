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
#include <sstream>
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

    RGBQUAD& at(int x, int y) { return pixels[static_cast<std::size_t>(y) * width + x]; }
    const RGBQUAD& at(int x, int y) const { return pixels[static_cast<std::size_t>(y) * width + x]; }
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
    if (fp) { std::fclose(fp); fp = nullptr; }
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
    const long sz = std::ftell(fp);
    if (sz < 0) return -1;
    if (std::fseek(fp, 0, SEEK_SET) != 0) return -1;
    return sz;
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

// ===== Boundary padding modes =====
enum PadMode { PAD_ZERO = 0, PAD_REPLICATE = 1, PAD_REFLECT = 2 };

static RGBQUAD getPixelPadded(const BmpImage& img, int x, int y, PadMode pad) {
    if (x >= 0 && x < img.width && y >= 0 && y < img.height) {
        return img.at(x, y);
    }
    switch (pad) {
        case PAD_ZERO: {
            RGBQUAD z{}; z.a = 255;
            return z;
        }
        case PAD_REPLICATE: {
            int cx = std::max(0, std::min(img.width - 1, x));
            int cy = std::max(0, std::min(img.height - 1, y));
            return img.at(cx, cy);
        }
        case PAD_REFLECT: {
            int cx = x, cy = y;
            if (cx < 0) cx = -cx - 1;
            if (cx >= img.width) cx = 2 * img.width - cx - 1;
            cx = std::max(0, std::min(img.width - 1, cx));
            if (cy < 0) cy = -cy - 1;
            if (cy >= img.height) cy = 2 * img.height - cy - 1;
            cy = std::max(0, std::min(img.height - 1, cy));
            return img.at(cx, cy);
        }
    }
    RGBQUAD z{}; z.a = 255;
    return z;
}

// ===== Parse kernel coefficients from string =====
// Format: comma-separated values row by row, e.g. "1,1,1,1,1,1,1,1,1" for 3x3 all-ones
static std::vector<double> parseKernel(const std::string& s, int expectedSize) {
    std::vector<double> k;
    std::istringstream iss(s);
    std::string token;
    while (std::getline(iss, token, ',')) {
        k.push_back(std::stod(token));
    }
    if (static_cast<int>(k.size()) != expectedSize) {
        throw std::runtime_error("Kernel size mismatch: expected " + std::to_string(expectedSize)
                                 + " values, got " + std::to_string(k.size()));
    }
    return k;
}

// ===== Mean filter with configurable parameters =====
static BmpImage meanFilter(const BmpImage& src, int ksize, const std::vector<double>& kernel,
                           int iterations, PadMode pad, int stride) {
    BmpImage current = src;
    const int half = ksize / 2;

    for (int iter = 0; iter < iterations; ++iter) {
        // Output dimensions depend on stride
        int outW = (current.width + stride - 1) / stride;
        int outH = (current.height + stride - 1) / stride;

        BmpImage next;
        next.width = outW;
        next.height = outH;
        next.pixels.resize(static_cast<std::size_t>(outW) * outH);

        for (int oy = 0; oy < outH; ++oy) {
            int cy = oy * stride;
            for (int ox = 0; ox < outW; ++ox) {
                int cx = ox * stride;

                double sumR = 0, sumG = 0, sumB = 0;
                double sumW = 0;

                for (int ky = 0; ky < ksize; ++ky) {
                    for (int kx = 0; kx < ksize; ++kx) {
                        int sx = cx + kx - half;
                        int sy = cy + ky - half;
                        RGBQUAD px = getPixelPadded(current, sx, sy, pad);
                        double w = kernel[static_cast<std::size_t>(ky) * ksize + kx];
                        sumR += w * px.r;
                        sumG += w * px.g;
                        sumB += w * px.b;
                        sumW += w;
                    }
                }

                // Normalize by sum of weights (if non-zero)
                if (std::fabs(sumW) > 1e-10) {
                    sumR /= sumW;
                    sumG /= sumW;
                    sumB /= sumW;
                }

                RGBQUAD out{};
                out.r = static_cast<BYTE>(std::max(0, std::min(255, static_cast<int>(sumR + 0.5))));
                out.g = static_cast<BYTE>(std::max(0, std::min(255, static_cast<int>(sumG + 0.5))));
                out.b = static_cast<BYTE>(std::max(0, std::min(255, static_cast<int>(sumB + 0.5))));
                out.a = 255;
                next.at(ox, oy) = out;
            }
        }

        current = next;
    }
    return current;
}

static void printUsage(const char* exe) {
    std::cout << "Usage:\n  " << exe
              << " <input.bmp> <output.bmp> <ksize> <kernel_csv> <iterations> <pad_mode> <stride>\n";
    std::cout << "  ksize: kernel dimension (e.g. 3, 5, 7)\n";
    std::cout << "  kernel_csv: comma-separated coefficients (ksize*ksize values)\n";
    std::cout << "    Presets: 'avg' = all-ones average, 'sharpen' = sharpening, 'edge' = edge detect\n";
    std::cout << "  iterations: number of filter passes (>=1)\n";
    std::cout << "  pad_mode: 0=zero, 1=replicate, 2=reflect\n";
    std::cout << "  stride: template movement interval (>=1)\n";
}

static std::vector<double> getPresetKernel(const std::string& name, int ksize) {
    int n = ksize * ksize;
    std::vector<double> k(n);

    if (name == "avg") {
        // All positive coefficients (uniform averaging)
        std::fill(k.begin(), k.end(), 1.0);
    } else if (name == "sharpen") {
        // Has positive and negative coefficients
        std::fill(k.begin(), k.end(), 0.0);
        int center = n / 2;
        k[center] = static_cast<double>(n);
        for (int i = 0; i < n; ++i) {
            if (i != center) k[i] = -1.0;
        }
    } else if (name == "edge") {
        // Has positive, negative, and zero coefficients
        if (ksize == 3) {
            // Laplacian-like: center positive, 4-connected negative, corners zero
            k = {0, -1, 0, -1, 4, -1, 0, -1, 0};
        } else {
            // Generalize: center = 2*(ksize-1), cross neighbors = -1, rest = 0
            std::fill(k.begin(), k.end(), 0.0);
            int half = ksize / 2;
            int centerIdx = half * ksize + half;
            for (int i = 0; i < ksize; ++i) {
                if (i != half) {
                    k[half * ksize + i] = -1.0;
                    k[i * ksize + half] = -1.0;
                }
            }
            double negSum = 0;
            for (double v : k) if (v < 0) negSum += v;
            k[centerIdx] = -negSum;
        }
    } else {
        throw std::runtime_error("Unknown kernel preset: " + name + ". Use avg, sharpen, or edge.");
    }
    return k;
}

int main(int argc, char* argv[]) {
    try {
        if (argc != 8) {
            if (argc == 1) {
                printUsage("bmp_meanfilter");
            } else {
                printUsage(argv[0]);
            }
            return 1;
        }

        std::string inputPath = argv[1];
        std::string outputPath = argv[2];
        int ksize = std::atoi(argv[3]);
        std::string kernelStr = argv[4];
        int iterations = std::atoi(argv[5]);
        int padInt = std::atoi(argv[6]);
        int stride = std::atoi(argv[7]);

        if (ksize < 1 || ksize % 2 == 0) {
            throw std::runtime_error("Kernel size must be a positive odd number.");
        }
        if (iterations < 1) throw std::runtime_error("Iterations must be >= 1.");
        if (padInt < 0 || padInt > 2) throw std::runtime_error("Pad mode must be 0, 1, or 2.");
        if (stride < 1) throw std::runtime_error("Stride must be >= 1.");

        PadMode pad = static_cast<PadMode>(padInt);

        // Parse or generate kernel
        std::vector<double> kernel;
        if (kernelStr == "avg" || kernelStr == "sharpen" || kernelStr == "edge") {
            kernel = getPresetKernel(kernelStr, ksize);
        } else {
            kernel = parseKernel(kernelStr, ksize * ksize);
        }

        std::cout << "Kernel (" << ksize << "x" << ksize << "):\n";
        for (int r = 0; r < ksize; ++r) {
            for (int c = 0; c < ksize; ++c) {
                std::cout << kernel[r * ksize + c];
                if (c < ksize - 1) std::cout << "\t";
            }
            std::cout << "\n";
        }
        std::cout << "Iterations: " << iterations << ", Pad: " << padInt
                  << ", Stride: " << stride << "\n\n";

        const auto t0 = std::chrono::steady_clock::now();
        BmpImage src = readBmp(inputPath);
        BmpImage dst = meanFilter(src, ksize, kernel, iterations, pad, stride);
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
