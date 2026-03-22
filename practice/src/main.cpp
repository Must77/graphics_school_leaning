#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

struct Vec2 {
    float x;
    float y;
};

struct Mat3 {
    // Row-major for CPU calculations.
    std::array<float, 9> m{};

    float& at(int r, int c) { return m[r * 3 + c]; }
    float at(int r, int c) const { return m[r * 3 + c]; }
};

static std::array<Vec2, 4> g_defaultQuad = {
    Vec2{-0.7f, -0.6f},
    Vec2{0.8f, -0.5f},
    Vec2{0.6f, 0.7f},
    Vec2{-0.8f, 0.6f},
};

static std::array<Vec2, 4> g_quad = g_defaultQuad;
static int g_selectedCorner = 0;

struct AppOptions {
    bool headless = false;
    int width = 960;
    int height = 720;
    int preset = 0;
    std::string outputPath;
};

static bool computeHomography(const std::array<Vec2, 4>& src, const std::array<Vec2, 4>& dst, Mat3& H);
static bool invertMat3(const Mat3& in, Mat3& out);

static void printUsage(const char* exe) {
    std::cout << "Usage:\n"
              << "  " << exe << "\n"
              << "  " << exe << " --headless --output <file.ppm> [--preset 0|1|2] [--width N] [--height N]\n\n"
              << "Options:\n"
              << "  --headless         Run one frame offscreen and exit.\n"
              << "  --output <path>    Output PPM path for headless mode.\n"
              << "  --preset <id>      Distortion preset id (0,1,2).\n"
              << "  --width <n>        Render width (default 960).\n"
              << "  --height <n>       Render height (default 720).\n"
              << "  --help             Show this message.\n";
}

static bool parseArgs(int argc, char** argv, AppOptions& opts) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--headless") {
            opts.headless = true;
        } else if (arg == "--output") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --output" << std::endl;
                return false;
            }
            opts.outputPath = argv[++i];
        } else if (arg == "--preset") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --preset" << std::endl;
                return false;
            }
            opts.preset = std::atoi(argv[++i]);
            if (opts.preset < 0 || opts.preset > 2) {
                std::cerr << "Preset must be 0, 1 or 2" << std::endl;
                return false;
            }
        } else if (arg == "--width") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --width" << std::endl;
                return false;
            }
            opts.width = std::atoi(argv[++i]);
            if (opts.width <= 0) {
                std::cerr << "Width must be > 0" << std::endl;
                return false;
            }
        } else if (arg == "--height") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --height" << std::endl;
                return false;
            }
            opts.height = std::atoi(argv[++i]);
            if (opts.height <= 0) {
                std::cerr << "Height must be > 0" << std::endl;
                return false;
            }
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return false;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            return false;
        }
    }

    if (opts.headless && opts.outputPath.empty()) {
        std::cerr << "--headless requires --output <file.ppm>" << std::endl;
        return false;
    }
    return true;
}

static std::array<Vec2, 4> presetQuad(int preset) {
    if (preset == 1) {
        return {
            Vec2{-0.95f, -0.7f},
            Vec2{0.9f, -0.3f},
            Vec2{0.45f, 0.9f},
            Vec2{-0.85f, 0.4f},
        };
    }
    if (preset == 2) {
        return {
            Vec2{-0.55f, -0.9f},
            Vec2{0.95f, -0.85f},
            Vec2{0.8f, 0.55f},
            Vec2{-0.7f, 0.95f},
        };
    }
    return g_defaultQuad;
}

static std::array<unsigned char, 3> sampleDemoTextureCPU(float u, float v) {
    const int w = 512;
    const int h = 512;

    u = std::max(0.0f, std::min(1.0f, u));
    v = std::max(0.0f, std::min(1.0f, v));

    const int x = std::min(w - 1, std::max(0, static_cast<int>(u * static_cast<float>(w - 1))));
    const int y = std::min(h - 1, std::max(0, static_cast<int>(v * static_cast<float>(h - 1))));

    const int checker = ((x / 32) + (y / 32)) % 2;
    unsigned char r = static_cast<unsigned char>((x * 255) / (w - 1));
    unsigned char g = static_cast<unsigned char>((y * 255) / (h - 1));
    unsigned char b = checker ? 210 : 70;

    if (std::abs(x - w / 2) < 3 || std::abs(y - h / 2) < 3) {
        r = 255;
        g = 255;
        b = 255;
    }
    return {r, g, b};
}

static bool writePPM(const std::string& path, int width, int height, const std::vector<unsigned char>& rgb) {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Failed to open output file: " << path << std::endl;
        return false;
    }

    out << "P6\n" << width << " " << height << "\n255\n";
    out.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    if (!out.good()) {
        std::cerr << "Failed while writing output file: " << path << std::endl;
        return false;
    }
    return true;
}

static bool renderHeadlessCPU(const AppOptions& opts) {
    const std::array<Vec2, 4> srcUv = {
        Vec2{0.0f, 0.0f},
        Vec2{1.0f, 0.0f},
        Vec2{1.0f, 1.0f},
        Vec2{0.0f, 1.0f},
    };

    Mat3 H{};
    Mat3 Hinv{};
    const auto quad = presetQuad(opts.preset);
    if (!computeHomography(srcUv, quad, H) || !invertMat3(H, Hinv)) {
        std::cerr << "Failed to compute homography for headless rendering" << std::endl;
        return false;
    }

    std::vector<unsigned char> rgb(static_cast<size_t>(opts.width) * static_cast<size_t>(opts.height) * 3u);
    const unsigned char bgR = static_cast<unsigned char>(0.06f * 255.0f);
    const unsigned char bgG = static_cast<unsigned char>(0.07f * 255.0f);
    const unsigned char bgB = static_cast<unsigned char>(0.08f * 255.0f);

    for (int py = 0; py < opts.height; ++py) {
        for (int px = 0; px < opts.width; ++px) {
            const float ndcX = 2.0f * ((static_cast<float>(px) + 0.5f) / static_cast<float>(opts.width)) - 1.0f;
            const float ndcY = 1.0f - 2.0f * ((static_cast<float>(py) + 0.5f) / static_cast<float>(opts.height));

            const float wx = Hinv.at(0, 0) * ndcX + Hinv.at(0, 1) * ndcY + Hinv.at(0, 2);
            const float wy = Hinv.at(1, 0) * ndcX + Hinv.at(1, 1) * ndcY + Hinv.at(1, 2);
            const float wz = Hinv.at(2, 0) * ndcX + Hinv.at(2, 1) * ndcY + Hinv.at(2, 2);

            const size_t idx = (static_cast<size_t>(py) * static_cast<size_t>(opts.width) + static_cast<size_t>(px)) * 3u;
            if (std::fabs(wz) < 1e-6f) {
                rgb[idx + 0] = bgR;
                rgb[idx + 1] = bgG;
                rgb[idx + 2] = bgB;
                continue;
            }

            const float u = wx / wz;
            const float v = wy / wz;
            if (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f) {
                const auto c = sampleDemoTextureCPU(u, v);
                rgb[idx + 0] = c[0];
                rgb[idx + 1] = c[1];
                rgb[idx + 2] = c[2];
            } else {
                rgb[idx + 0] = bgR;
                rgb[idx + 1] = bgG;
                rgb[idx + 2] = bgB;
            }
        }
    }

    return writePPM(opts.outputPath, opts.width, opts.height, rgb);
}

static bool saveFramebufferPPM(const std::string& path, int width, int height) {
    std::vector<unsigned char> rgb(static_cast<size_t>(width) * static_cast<size_t>(height) * 3u);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_FRONT);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Failed to open output file: " << path << std::endl;
        return false;
    }

    out << "P6\n" << width << " " << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y) {
        const unsigned char* row = &rgb[static_cast<size_t>(y) * static_cast<size_t>(width) * 3u];
        out.write(reinterpret_cast<const char*>(row), static_cast<std::streamsize>(width * 3));
    }

    if (!out.good()) {
        std::cerr << "Failed while writing output file: " << path << std::endl;
        return false;
    }
    return true;
}

static bool solveLinear8x8(float A[8][8], float b[8], float x[8]) {
    // Gaussian elimination with partial pivoting.
    for (int col = 0; col < 8; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 8; ++row) {
            if (std::fabs(A[row][col]) > std::fabs(A[pivot][col])) {
                pivot = row;
            }
        }

        if (std::fabs(A[pivot][col]) < 1e-8f) {
            return false;
        }

        if (pivot != col) {
            for (int k = col; k < 8; ++k) {
                std::swap(A[col][k], A[pivot][k]);
            }
            std::swap(b[col], b[pivot]);
        }

        float diag = A[col][col];
        for (int k = col; k < 8; ++k) {
            A[col][k] /= diag;
        }
        b[col] /= diag;

        for (int row = 0; row < 8; ++row) {
            if (row == col) {
                continue;
            }
            float factor = A[row][col];
            for (int k = col; k < 8; ++k) {
                A[row][k] -= factor * A[col][k];
            }
            b[row] -= factor * b[col];
        }
    }

    for (int i = 0; i < 8; ++i) {
        x[i] = b[i];
    }
    return true;
}

static bool computeHomography(const std::array<Vec2, 4>& src, const std::array<Vec2, 4>& dst, Mat3& H) {
    float A[8][8]{};
    float b[8]{};

    for (int i = 0; i < 4; ++i) {
        const float x = src[i].x;
        const float y = src[i].y;
        const float u = dst[i].x;
        const float v = dst[i].y;

        int r = 2 * i;
        A[r][0] = x;
        A[r][1] = y;
        A[r][2] = 1.0f;
        A[r][6] = -u * x;
        A[r][7] = -u * y;
        b[r] = u;

        A[r + 1][3] = x;
        A[r + 1][4] = y;
        A[r + 1][5] = 1.0f;
        A[r + 1][6] = -v * x;
        A[r + 1][7] = -v * y;
        b[r + 1] = v;
    }

    float h[8]{};
    if (!solveLinear8x8(A, b, h)) {
        return false;
    }

    H.at(0, 0) = h[0];
    H.at(0, 1) = h[1];
    H.at(0, 2) = h[2];
    H.at(1, 0) = h[3];
    H.at(1, 1) = h[4];
    H.at(1, 2) = h[5];
    H.at(2, 0) = h[6];
    H.at(2, 1) = h[7];
    H.at(2, 2) = 1.0f;
    return true;
}

static bool invertMat3(const Mat3& in, Mat3& out) {
    const float a = in.at(0, 0), b = in.at(0, 1), c = in.at(0, 2);
    const float d = in.at(1, 0), e = in.at(1, 1), f = in.at(1, 2);
    const float g = in.at(2, 0), h = in.at(2, 1), i = in.at(2, 2);

    const float A = (e * i - f * h);
    const float B = -(d * i - f * g);
    const float C = (d * h - e * g);
    const float D = -(b * i - c * h);
    const float E = (a * i - c * g);
    const float F = -(a * h - b * g);
    const float G = (b * f - c * e);
    const float H = -(a * f - c * d);
    const float I = (a * e - b * d);

    const float det = a * A + b * B + c * C;
    if (std::fabs(det) < 1e-8f) {
        return false;
    }

    const float invDet = 1.0f / det;
    out.at(0, 0) = A * invDet;
    out.at(0, 1) = D * invDet;
    out.at(0, 2) = G * invDet;
    out.at(1, 0) = B * invDet;
    out.at(1, 1) = E * invDet;
    out.at(1, 2) = H * invDet;
    out.at(2, 0) = C * invDet;
    out.at(2, 1) = F * invDet;
    out.at(2, 2) = I * invDet;
    return true;
}

static GLuint compileShader(GLenum type, const std::string& src) {
    GLuint shader = glCreateShader(type);
    const char* cstr = src.c_str();
    glShaderSource(shader, 1, &cstr, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(logLen);
        glGetShaderInfoLog(shader, logLen, nullptr, log.data());
        std::cerr << "Shader compile error:\n" << log.data() << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint makeProgram(const std::string& vs, const std::string& fs) {
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) {
        if (v) {
            glDeleteShader(v);
        }
        if (f) {
            glDeleteShader(f);
        }
        return 0;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    glLinkProgram(prog);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(logLen);
        glGetProgramInfoLog(prog, logLen, nullptr, log.data());
        std::cerr << "Program link error:\n" << log.data() << std::endl;
        glDeleteProgram(prog);
        prog = 0;
    }

    glDeleteShader(v);
    glDeleteShader(f);
    return prog;
}

static GLuint createDemoTexture() {
    const int w = 512;
    const int h = 512;
    std::vector<unsigned char> pixels(w * h * 3);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int idx = (y * w + x) * 3;
            const int checker = ((x / 32) + (y / 32)) % 2;

            unsigned char r = static_cast<unsigned char>((x * 255) / (w - 1));
            unsigned char g = static_cast<unsigned char>((y * 255) / (h - 1));
            unsigned char b = checker ? 210 : 70;

            if (std::abs(x - w / 2) < 3 || std::abs(y - h / 2) < 3) {
                r = 255;
                g = 255;
                b = 255;
            }

            pixels[idx + 0] = r;
            pixels[idx + 1] = g;
            pixels[idx + 2] = b;
        }
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

static std::array<float, 9> toColumnMajor(const Mat3& M) {
    return {
        M.at(0, 0), M.at(1, 0), M.at(2, 0),
        M.at(0, 1), M.at(1, 1), M.at(2, 1),
        M.at(0, 2), M.at(1, 2), M.at(2, 2),
    };
}

static void keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS) {
        return;
    }

    if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    } else if (key == GLFW_KEY_1) {
        g_selectedCorner = 0;
    } else if (key == GLFW_KEY_2) {
        g_selectedCorner = 1;
    } else if (key == GLFW_KEY_3) {
        g_selectedCorner = 2;
    } else if (key == GLFW_KEY_4) {
        g_selectedCorner = 3;
    } else if (key == GLFW_KEY_R) {
        g_quad = g_defaultQuad;
    }
}

int main(int argc, char** argv) {
    AppOptions opts;
    if (!parseArgs(argc, argv, opts)) {
        if (argc == 1) {
            // No args is normal interactive mode.
        } else {
            return 1;
        }
    }

    if (opts.headless) {
        if (!renderHeadlessCPU(opts)) {
            return 1;
        }
        std::cout << "Saved headless image to " << opts.outputPath << std::endl;
        return 0;
    }

    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW" << std::endl;
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    if (!opts.headless) {
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    }
    if (opts.headless) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    GLFWwindow* window = glfwCreateWindow(opts.width, opts.height, "Perspective Distortion", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, keyCallback);

    glewExperimental = GL_TRUE;
    GLenum glewStatus = glewInit();
    if (glewStatus != GLEW_OK) {
        std::cerr << "Failed to init GLEW: " << glewGetErrorString(glewStatus) << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // GLEW may trigger a benign GL error during initialization.
    glGetError();

    glViewport(0, 0, opts.width, opts.height);

    const std::string warpVS = R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        out vec2 vNdc;
        void main() {
            vNdc = aPos;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    const std::string warpFS = R"(
        #version 330 core
        in vec2 vNdc;
        out vec4 FragColor;

        uniform sampler2D uTex;
        uniform mat3 uHinv;

        void main() {
            vec3 uvh = uHinv * vec3(vNdc, 1.0);
            if (abs(uvh.z) < 1e-6) {
                FragColor = vec4(0.06, 0.07, 0.08, 1.0);
                return;
            }

            vec2 uv = uvh.xy / uvh.z;
            if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0) {
                vec3 c = texture(uTex, uv).rgb;
                FragColor = vec4(c, 1.0);
            } else {
                FragColor = vec4(0.06, 0.07, 0.08, 1.0);
            }
        }
    )";

    const std::string overlayVS = R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        void main() {
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    const std::string overlayFS = R"(
        #version 330 core
        out vec4 FragColor;
        uniform vec3 uColor;
        void main() {
            FragColor = vec4(uColor, 1.0);
        }
    )";

    GLuint warpProgram = makeProgram(warpVS, warpFS);
    GLuint overlayProgram = makeProgram(overlayVS, overlayFS);
    if (!warpProgram || !overlayProgram) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    float fullscreenQuad[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f,
    };

    GLuint fsVAO = 0;
    GLuint fsVBO = 0;
    glGenVertexArrays(1, &fsVAO);
    glGenBuffers(1, &fsVBO);

    glBindVertexArray(fsVAO);
    glBindBuffer(GL_ARRAY_BUFFER, fsVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(fullscreenQuad), fullscreenQuad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);

    GLuint ctrlVAO = 0;
    GLuint ctrlVBO = 0;
    glGenVertexArrays(1, &ctrlVAO);
    glGenBuffers(1, &ctrlVBO);
    glBindVertexArray(ctrlVAO);
    glBindBuffer(GL_ARRAY_BUFFER, ctrlVBO);
    glBufferData(GL_ARRAY_BUFFER, 4 * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);

    GLuint tex = createDemoTexture();
    GLint hInvLoc = glGetUniformLocation(warpProgram, "uHinv");
    GLint colorLoc = glGetUniformLocation(overlayProgram, "uColor");

    const std::array<Vec2, 4> srcUv = {
        Vec2{0.0f, 0.0f},
        Vec2{1.0f, 0.0f},
        Vec2{1.0f, 1.0f},
        Vec2{0.0f, 1.0f},
    };

    float lastTime = static_cast<float>(glfwGetTime());

    while (!glfwWindowShouldClose(window)) {
        const float now = static_cast<float>(glfwGetTime());
        const float dt = now - lastTime;
        lastTime = now;

        glfwPollEvents();

        const float speed = 0.7f;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
            g_quad[g_selectedCorner].y += speed * dt;
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
            g_quad[g_selectedCorner].y -= speed * dt;
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
            g_quad[g_selectedCorner].x -= speed * dt;
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
            g_quad[g_selectedCorner].x += speed * dt;
        }

        g_quad[g_selectedCorner].x = std::max(-0.98f, std::min(0.98f, g_quad[g_selectedCorner].x));
        g_quad[g_selectedCorner].y = std::max(-0.98f, std::min(0.98f, g_quad[g_selectedCorner].y));

        Mat3 H{};
        Mat3 Hinv{};
        bool ok = computeHomography(srcUv, g_quad, H) && invertMat3(H, Hinv);

        std::array<float, 8> flat{};
        for (int i = 0; i < 4; ++i) {
            flat[2 * i + 0] = g_quad[i].x;
            flat[2 * i + 1] = g_quad[i].y;
        }

        glBindBuffer(GL_ARRAY_BUFFER, ctrlVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(float) * flat.size(), flat.data());

        glClearColor(0.06f, 0.07f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(warpProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUniform1i(glGetUniformLocation(warpProgram, "uTex"), 0);

        if (ok) {
            const auto colMajor = toColumnMajor(Hinv);
            glUniformMatrix3fv(hInvLoc, 1, GL_FALSE, colMajor.data());
        }

        glBindVertexArray(fsVAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glUseProgram(overlayProgram);
        glBindVertexArray(ctrlVAO);

        glUniform3f(colorLoc, 0.15f, 0.9f, 0.95f);
        glDrawArrays(GL_LINE_LOOP, 0, 4);

        glPointSize(9.0f);
        glUniform3f(colorLoc, 1.0f, 0.3f, 0.2f);
        glDrawArrays(GL_POINTS, 0, 4);

        glPointSize(13.0f);
        glUniform3f(colorLoc, 1.0f, 1.0f, 0.1f);
        glDrawArrays(GL_POINTS, g_selectedCorner, 1);

        glfwSwapBuffers(window);

        if (opts.headless) {
            if (!saveFramebufferPPM(opts.outputPath, opts.width, opts.height)) {
                glfwDestroyWindow(window);
                glfwTerminate();
                return 1;
            }
            break;
        }
    }

    glDeleteTextures(1, &tex);
    glDeleteBuffers(1, &fsVBO);
    glDeleteVertexArrays(1, &fsVAO);
    glDeleteBuffers(1, &ctrlVBO);
    glDeleteVertexArrays(1, &ctrlVAO);
    glDeleteProgram(warpProgram);
    glDeleteProgram(overlayProgram);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
