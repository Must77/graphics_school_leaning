计算机图像学课程任务：图像变换（透视失真）

本项目使用 C++14 + OpenGL（底层图形 API）实现图像透视失真实验。

## 实验目标

1. 使用底层图形 API 实现图像映射与变换。
2. 实现四点控制的透视失真效果。
3. 可交互调整目标四边形，实时观察失真结果。

## 实现说明

1. 源图像使用程序生成纹理（棋盘格 + 颜色渐变 + 十字参考线）。
2. 通过 4 对对应点计算单应矩阵（Homography）。
3. 在片元着色器中使用逆单应矩阵，将屏幕点反算回纹理坐标并采样。

## 目录结构

```text
.
├── CMakeLists.txt
├── README.md
└── src
	└── main.cpp
```

## 依赖环境（Ubuntu/Linux）

```bash
sudo apt update
sudo apt install -y build-essential cmake libglfw3-dev libglew-dev libgl1-mesa-dev
```

## 编译与运行

```bash
cmake -S . -B build
cmake --build build -j
./build/perspective_distortion
```

## 自动化生成图片与测试

项目已支持无头模式输出 PPM 图片（CPU 渲染路径，无需显示环境与手动操作窗口）。

1. 直接运行自动化脚本（会生成 3 张预设透视失真图并做校验）：

```bash
python3 scripts/auto_test.py
```

2. 输出目录默认在 `outputs/`，文件名示例：

- `outputs/distortion_preset_0.ppm`
- `outputs/distortion_preset_1.ppm`
- `outputs/distortion_preset_2.ppm`

3. 如需手动调用无头截图：

```bash
./build/perspective_distortion --headless --output outputs/manual.ppm --preset 1 --width 960 --height 720
```

## 交互说明

1. `1/2/3/4`：选择控制点。
2. `W/A/S/D` 或方向键：移动当前控制点。
3. `R`：重置控制点到初始位置。
4. `ESC`：退出。

## 验收要点

1. 能实时看到纹理在任意四边形上的透视失真。
2. 控制点移动后，图像映射连续稳定。
3. 符合 C++11/14 + 底层图形 API 的课程要求。