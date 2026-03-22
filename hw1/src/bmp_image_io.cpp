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

static void printUsage(const std::string& exeName) {
    std::cout << "Usage (command line):\n";
    std::cout << "  " << exeName << " <input.bmp> <mode> <output.bmp>\n";
    std::cout << "  mode: 1=Invert, 2=Grayscale\n\n";
    std::cout << "Or run without parameters to use interactive mode.\n";
}

static void printHeaders(const BITMAPFILEHEADER& fileHeader, const BITMAPINFOHEADER& infoHeader) {
    std::cout << "[BMP FILE HEADER]\n";
    std::cout << "bfType: 0x" << std::hex << fileHeader.bfType << std::dec << "\n";
    std::cout << "bfSize: " << fileHeader.bfSize << "\n";
    std::cout << "bfOffBits: " << fileHeader.bfOffBits << "\n\n";

    std::cout << "[BMP INFO HEADER]\n";
    std::cout << "biSize: " << infoHeader.biSize << "\n";
    std::cout << "biWidth: " << infoHeader.biWidth << "\n";
    std::cout << "biHeight: " << infoHeader.biHeight << "\n";
    std::cout << "biPlanes: " << infoHeader.biPlanes << "\n";
    std::cout << "biBitCount: " << infoHeader.biBitCount << "\n";
    std::cout << "biCompression: " << infoHeader.biCompression << "\n";
    std::cout << "biSizeImage: " << infoHeader.biSizeImage << "\n\n";

    if (infoHeader.biBitCount == 24) {
        std::cout << "Pixel format detail: 24-bit BGR, 3 bytes per pixel.\n";
    } else if (infoHeader.biBitCount == 32) {
        std::cout << "Pixel format detail: 32-bit BGRA, 4 bytes per pixel.\n";
    }
}

static long getFileSize(FILE* fp) {
    if (std::fseek(fp, 0, SEEK_END) != 0) {
        return -1;
    }
    const long size = std::ftell(fp);
    if (size < 0) {
        return -1;
    }
    if (std::fseek(fp, 0, SEEK_SET) != 0) {
        return -1;
    }
    return size;
}

static BmpImage readBmp(const std::string& inputPath) {
    FILE* fp = std::fopen(inputPath.c_str(), "rb");
    if (!fp) {
        throw std::runtime_error("Cannot open input BMP file: " + inputPath);
    }

    BITMAPFILEHEADER fileHeader{};
    BITMAPINFOHEADER infoHeader{};

    if (std::fread(&fileHeader, sizeof(fileHeader), 1, fp) != 1) {
        closeFile(fp);
        throw std::runtime_error("Failed to read BITMAPFILEHEADER.");
    }

    if (std::fread(&infoHeader, sizeof(infoHeader), 1, fp) != 1) {
        closeFile(fp);
        throw std::runtime_error("Failed to read BITMAPINFOHEADER.");
    }

    if (fileHeader.bfType != 0x4D42) {
        closeFile(fp);
        throw std::runtime_error("Input file is not a BMP file.");
    }

    if (infoHeader.biSize < sizeof(BITMAPINFOHEADER)) {
        closeFile(fp);
        throw std::runtime_error("Unsupported BMP info header size.");
    }

    if (infoHeader.biPlanes != 1) {
        closeFile(fp);
        throw std::runtime_error("Invalid BMP planes value (must be 1).");
    }

    if (infoHeader.biCompression != 0) {
        closeFile(fp);
        throw std::runtime_error("Only uncompressed BMP (BI_RGB) is supported.");
    }

    if (infoHeader.biBitCount != 24 && infoHeader.biBitCount != 32) {
        closeFile(fp);
        throw std::runtime_error("Only 24-bit and 32-bit BMP are supported.");
    }

    const long fileSize = getFileSize(fp);
    if (fileSize <= 0) {
        closeFile(fp);
        throw std::runtime_error("Failed to query input file size.");
    }

    printHeaders(fileHeader, infoHeader);

    const int width = static_cast<int>(infoHeader.biWidth);
    const int heightAbs = static_cast<int>(infoHeader.biHeight < 0 ? -infoHeader.biHeight : infoHeader.biHeight);
    const bool topDown = (infoHeader.biHeight < 0);

    if (width <= 0 || heightAbs <= 0) {
        closeFile(fp);
        throw std::runtime_error("Invalid BMP image dimensions.");
    }

    const int bitsPerPixel = static_cast<int>(infoHeader.biBitCount);
    const int srcStride = rowStrideBytes(width, bitsPerPixel);

    const long requiredBytes = static_cast<long>(fileHeader.bfOffBits) + static_cast<long>(srcStride) * heightAbs;
    if (requiredBytes > fileSize) {
        closeFile(fp);
        throw std::runtime_error("BMP file is truncated: pixel data exceeds file size.");
    }

    if (fileHeader.bfOffBits < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)) {
        closeFile(fp);
        throw std::runtime_error("Invalid pixel data offset (bfOffBits).");
    }

    if (std::fseek(fp, static_cast<long>(fileHeader.bfOffBits), SEEK_SET) != 0) {
        closeFile(fp);
        throw std::runtime_error("Failed to seek to pixel data.");
    }

    std::vector<BYTE> rowBuffer(static_cast<std::size_t>(srcStride));
    BmpImage image;
    image.width = width;
    image.height = heightAbs;
    image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(heightAbs));

    for (int srcY = 0; srcY < heightAbs; ++srcY) {
        if (std::fread(rowBuffer.data(), 1, static_cast<std::size_t>(srcStride), fp) != static_cast<std::size_t>(srcStride)) {
            closeFile(fp);
            throw std::runtime_error("Failed while reading BMP pixel rows.");
        }

        const int dstY = topDown ? srcY : (heightAbs - 1 - srcY);
        for (int x = 0; x < width; ++x) {
            const int srcOffset = x * (bitsPerPixel / 8);
            RGBQUAD px{};
            px.b = rowBuffer[static_cast<std::size_t>(srcOffset) + 0];
            px.g = rowBuffer[static_cast<std::size_t>(srcOffset) + 1];
            px.r = rowBuffer[static_cast<std::size_t>(srcOffset) + 2];
            px.a = (bitsPerPixel == 32) ? rowBuffer[static_cast<std::size_t>(srcOffset) + 3] : static_cast<BYTE>(255);
            image.pixels[static_cast<std::size_t>(dstY) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = px;
        }
    }

    closeFile(fp);
    return image;
}

static void writeBmp24(const std::string& outputPath, const BmpImage& image) {
    FILE* fp = std::fopen(outputPath.c_str(), "wb");
    if (!fp) {
        throw std::runtime_error("Cannot open output BMP file: " + outputPath);
    }

    const int width = image.width;
    const int height = image.height;
    if (width <= 0 || height <= 0) {
        closeFile(fp);
        throw std::runtime_error("Cannot write empty image.");
    }

    const int dstStride = rowStrideBytes(width, 24);
    const DWORD pixelDataSize = static_cast<DWORD>(dstStride * height);

    BITMAPFILEHEADER fileHeader{};
    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + pixelDataSize;

    BITMAPINFOHEADER infoHeader{};
    infoHeader.biSize = sizeof(BITMAPINFOHEADER);
    infoHeader.biWidth = width;
    infoHeader.biHeight = height;
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 24;
    infoHeader.biCompression = 0;
    infoHeader.biSizeImage = pixelDataSize;

    if (std::fwrite(&fileHeader, sizeof(fileHeader), 1, fp) != 1 ||
        std::fwrite(&infoHeader, sizeof(infoHeader), 1, fp) != 1) {
        closeFile(fp);
        throw std::runtime_error("Failed to write BMP headers.");
    }

    std::vector<BYTE> rowBuffer(static_cast<std::size_t>(dstStride), static_cast<BYTE>(0));

    for (int y = height - 1; y >= 0; --y) {
        std::fill(rowBuffer.begin(), rowBuffer.end(), static_cast<BYTE>(0));
        for (int x = 0; x < width; ++x) {
            const RGBQUAD& px = image.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
            const int offset = x * 3;
            rowBuffer[static_cast<std::size_t>(offset) + 0] = px.b;
            rowBuffer[static_cast<std::size_t>(offset) + 1] = px.g;
            rowBuffer[static_cast<std::size_t>(offset) + 2] = px.r;
        }

        if (std::fwrite(rowBuffer.data(), 1, rowBuffer.size(), fp) != rowBuffer.size()) {
            closeFile(fp);
            throw std::runtime_error("Failed while writing BMP pixel rows.");
        }
    }

    closeFile(fp);
}

static BmpImage invertColor(const BmpImage& src) {
    BmpImage dst = src;
    for (auto& px : dst.pixels) {
        px.r = static_cast<BYTE>(255 - px.r);
        px.g = static_cast<BYTE>(255 - px.g);
        px.b = static_cast<BYTE>(255 - px.b);
    }
    return dst;
}

static BmpImage toGray(const BmpImage& src) {
    BmpImage dst = src;
    for (auto& px : dst.pixels) {
        const int gray = static_cast<int>(0.299 * px.r + 0.587 * px.g + 0.114 * px.b + 0.5);
        const BYTE g = static_cast<BYTE>(gray > 255 ? 255 : (gray < 0 ? 0 : gray));
        px.r = g;
        px.g = g;
        px.b = g;
    }
    return dst;
}

static GrayStats calcGrayStats(const BmpImage& image) {
    GrayStats s;
    if (image.pixels.empty()) {
        return s;
    }

    double sum = 0.0;
    double sumSq = 0.0;
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

static void parseArgsOrInteractive(
    int argc,
    char* argv[],
    std::string& inputPath,
    std::string& outputPath,
    int& mode) {

    if (argc == 4) {
        inputPath = argv[1];
        mode = std::atoi(argv[2]);
        outputPath = argv[3];
        return;
    }

    if (argc != 1) {
        printUsage(argv[0]);
        throw std::runtime_error("Invalid arguments. Expected 3 arguments or interactive mode.");
    }

    std::cout << "Enter source BMP path: ";
    std::getline(std::cin, inputPath);
    if (inputPath.empty()) {
        throw std::runtime_error("Input path is empty.");
    }

    std::cout << "Select process mode (1=Invert, 2=Grayscale): ";
    std::cin >> mode;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    std::cout << "Enter destination BMP path: ";
    std::getline(std::cin, outputPath);
    if (outputPath.empty()) {
        throw std::runtime_error("Output path is empty.");
    }
}

int main(int argc, char* argv[]) {
    try {
        std::string inputPath;
        std::string outputPath;
        int mode = 0;

        parseArgsOrInteractive(argc, argv, inputPath, outputPath, mode);

        if (mode != 1 && mode != 2) {
            throw std::runtime_error("Invalid mode, please choose 1 or 2.");
        }

        const auto t0 = std::chrono::steady_clock::now();

        const BmpImage src = readBmp(inputPath);
        const BmpImage dst = (mode == 1) ? invertColor(src) : toGray(src);
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
