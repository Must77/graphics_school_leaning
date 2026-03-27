# 自动化实验数据

生成时间：2026-03-27 17:41:02

测试程序：`bmp_meanfilter`

| 用例 | 分辨率 | 配置 | 模板 | 迭代 | 填充 | 步长 | 耗时(ms) | min | max | mean | stddev | rc |
|---|---|---|---|---:|---|---:|---:|---:|---:|---:|---:|---:|
| A_small_256x256.bmp | 256x256 | avg3x3_1x_rep | 3x3 | 1 | replicate | 1 | 6 | 0 | 255 | 127.4710 | 54.3794 | 0 |
| A_small_256x256.bmp | 256x256 | avg3x3_3x_rep | 3x3 | 3 | replicate | 1 | 13 | 0 | 255 | 127.4710 | 54.3794 | 0 |
| A_small_256x256.bmp | 256x256 | avg5x5_1x_rep | 5x5 | 1 | replicate | 1 | 9 | 1 | 254 | 127.4720 | 54.3695 | 0 |
| A_small_256x256.bmp | 256x256 | sharpen3x3_1x_rep | 3x3 | 1 | replicate | 1 | 5 | 0 | 255 | 127.4720 | 54.3829 | 0 |
| A_small_256x256.bmp | 256x256 | edge3x3_1x_zero | 3x3 | 1 | zero | 1 | 4 | 0 | 255 | 2.0966 | 18.4416 | 0 |
| A_small_256x256.bmp | 256x256 | avg3x3_1x_reflect | 3x3 | 1 | reflect | 1 | 6 | 0 | 255 | 127.4710 | 54.3794 | 0 |
| A_small_256x256.bmp | 256x256 | avg3x3_1x_stride2 | 3x3 | 1 | replicate | 2 | 9 | 0 | 254 | 127.0000 | 54.3781 | 0 |
| B_mid_1024x768.bmp | 1024x768 | avg3x3_1x_rep | 3x3 | 1 | replicate | 1 | 62 | 102 | 154 | 128.2050 | 24.5325 | 0 |
| B_mid_1024x768.bmp | 1024x768 | avg3x3_3x_rep | 3x3 | 3 | replicate | 1 | 145 | 102 | 154 | 128.2050 | 23.4337 | 0 |
| B_mid_1024x768.bmp | 1024x768 | avg5x5_1x_rep | 5x5 | 1 | replicate | 1 | 119 | 102 | 154 | 128.2050 | 23.4224 | 0 |
| B_mid_1024x768.bmp | 1024x768 | sharpen3x3_1x_rep | 3x3 | 1 | replicate | 1 | 57 | 92 | 166 | 128.2890 | 27.4031 | 0 |
| B_mid_1024x768.bmp | 1024x768 | edge3x3_1x_zero | 3x3 | 1 | zero | 1 | 59 | 0 | 203 | 9.9976 | 28.6921 | 0 |
| B_mid_1024x768.bmp | 1024x768 | avg3x3_1x_reflect | 3x3 | 1 | reflect | 1 | 74 | 102 | 154 | 128.2050 | 24.5325 | 0 |
| B_mid_1024x768.bmp | 1024x768 | avg3x3_1x_stride2 | 3x3 | 1 | replicate | 2 | 26 | 102 | 154 | 128.2050 | 24.5325 | 0 |
| C_large_1920x1080.bmp | 1920x1080 | avg3x3_1x_rep | 3x3 | 1 | replicate | 1 | 145 | 4 | 240 | 127.4990 | 43.3882 | 0 |
| C_large_1920x1080.bmp | 1920x1080 | avg3x3_3x_rep | 3x3 | 3 | replicate | 1 | 379 | 10 | 220 | 127.4990 | 39.1429 | 0 |
| C_large_1920x1080.bmp | 1920x1080 | avg5x5_1x_rep | 5x5 | 1 | replicate | 1 | 309 | 9 | 226 | 127.4990 | 38.9371 | 0 |
| C_large_1920x1080.bmp | 1920x1080 | sharpen3x3_1x_rep | 3x3 | 1 | replicate | 1 | 146 | 0 | 255 | 127.4990 | 50.3125 | 0 |
| C_large_1920x1080.bmp | 1920x1080 | edge3x3_1x_zero | 3x3 | 1 | zero | 1 | 157 | 0 | 255 | 10.3308 | 34.0704 | 0 |
| C_large_1920x1080.bmp | 1920x1080 | avg3x3_1x_reflect | 3x3 | 1 | reflect | 1 | 157 | 4 | 240 | 127.4990 | 43.3882 | 0 |
| C_large_1920x1080.bmp | 1920x1080 | avg3x3_1x_stride2 | 3x3 | 1 | replicate | 2 | 51 | 4 | 237 | 127.4760 | 43.3915 | 0 |

说明：若返回码为 0 表示执行成功。
