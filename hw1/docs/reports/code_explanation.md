# BMP 读写代码说明（简要）

本文档用于简要说明 `dipcode.cpp` 的实现思路与主要函数。

## 1. 代码实现了什么

该程序使用 C++11/14 和底层文件 API（`fopen`、`fread`、`fwrite`）实现了 BMP 图像的读取、处理和写出，支持：

- 读取无压缩 BMP（`24-bit` / `32-bit`）
- 图像反色（Invert）
- 图像灰度化（Grayscale）
- 结果按 `24-bit BMP` 输出

## 2. 关键数据结构

- `BITMAPFILEHEADER`：BMP 文件头，包含文件类型、文件大小、像素数据偏移等信息。
- `BITMAPINFOHEADER`：BMP 信息头，包含宽高、位深、压缩方式等信息。
- `RGBQUAD`：单个像素的 BGRA 通道。
- `BmpImage`：程序内部图像对象，包含 `width`、`height` 和像素数组 `pixels`。

## 3. 主要函数说明

- `rowStrideBytes(int width, int bitsPerPixel)`
  计算 BMP 每行实际字节数（按 4 字节对齐）。

- `readBmp(const std::string& inputPath)`
  读取 BMP 文件头和像素数据，做格式合法性检查，并转换到统一内存结构 `BmpImage`。

- `writeBmp24(const std::string& outputPath, const BmpImage& image)`
  将内存中的图像按 24 位 BMP 格式写出，自动处理行对齐和底向上存储。

- `invertColor(const BmpImage& src)`
  对每个像素执行 `255 - 通道值`，得到反色图像。

- `toGray(const BmpImage& src)`
  按公式 `Gray = 0.299R + 0.587G + 0.114B` 计算灰度值，并写回三个颜色通道。

## 4. 程序运行流程

1. 输入源 BMP 路径。
2. 选择处理模式（`1=反色`，`2=灰度`）。
3. 输入输出 BMP 路径。
4. 程序读取图像、执行处理、保存结果。

## 5. 编译与运行（Windows + VS2022）

在 VS2022 开发者命令环境中可使用：

```bat
cl /std:c++14 /EHsc dipcode.cpp /Fe:bmp_lab_vs.exe
bmp_lab_vs.exe
```

## 6. 注意事项

- 当前实现只支持无压缩 BMP（`biCompression = 0`）。
- 若输入不是 24/32 位 BMP，程序会抛出错误提示。
- 输出统一为 24 位 BMP，兼容性较好。
