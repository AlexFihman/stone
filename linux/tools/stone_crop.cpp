// stone_crop: one pixel per cell of f(x,y), colours as in stone_height.
// Usage: stone_crop DIR N H X0 Y0 SIZE out.ppm   (below the diagonal: Y0+SIZE <= X0, or on it: X0 = Y0)
// Build: -O3 -march=native -lzstd
#include <zstd.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static void hsv(float h, float s, float v, float* c)
{
    const float k[3] = {5, 3, 1};
    for (int a = 0; a < 3; ++a) {
        const float q = std::fmod(k[a] + h * 6, 6.f);
        c[a] = v - v * s * std::max(0.f, std::min({q, 4 - q, 1.f}));
    }
}

int main(int argc, char** argv)
{
    if (argc != 8) { std::fprintf(stderr, "usage: stone_crop DIR N H X0 Y0 SIZE out.ppm\n"); return 2; }
    const std::string dir = argv[1];
    const int n = std::atoi(argv[2]), H = std::atoi(argv[3]), X = std::atoi(argv[4]), Y = std::atoi(argv[5]), S = std::atoi(argv[6]);
    int B = 64;
    while (H % B) --B;
    const int L = 4096;
    if (S <= 0 || X < 0 || Y < 0 || X + S > n || (Y + S > X && X != Y)) { std::fprintf(stderr, "bad window\n"); return 2; }
    const float lo = -5, hi = 1; // log2 ratio range of the lookup table
    std::vector<float> lut(3 * L);
    for (int a = 0; a < L; ++a) {
        const float r = lo + (hi - lo) * a / (L - 1), t = std::max(0.f, (r + 1) / 2);
        const float fade = r < -1 ? (r - lo) / (-1 - lo) : 1;
        hsv(.72f * (1 - t), .95f, fade * (.25f + .75f * std::min(1.f, 1.6f * t + .2f)), &lut[3 * a]);
    }
    std::vector<unsigned char> img(std::size_t(S) * S * 3);
    for (int y0 = Y / H * H; y0 < Y + S; y0 += H) {
        const int h = std::min(H, n - y0);
        std::FILE* f = std::fopen((dir + "/out_" + std::to_string(y0) + ".zst").c_str(), "rb");
        if (!f) { std::perror("open strip"); return 1; }
        for (int x0 = y0; x0 < X + S; x0 += B) {
            std::uint64_t hd[2];
            if (std::fread(hd, 1, 16, f) != 16) { std::fprintf(stderr, "truncated\n"); return 1; }
            const int x1 = std::min(n, x0 + B);
            if (x1 <= X) { if (fseeko(f, off_t(hd[1]), SEEK_CUR)) return 1; continue; }
            std::vector<char> z(hd[1]);
            std::vector<unsigned char> raw(hd[0]);
            if (std::fread(z.data(), 1, z.size(), f) != z.size() ||
                ZSTD_decompress(raw.data(), raw.size(), z.data(), z.size()) != raw.size()) { std::fprintf(stderr, "corrupt\n"); return 1; }
            const std::size_t cells = raw.size() / 3;
            for (int j = std::max(x0, X); j < std::min(x1, X + S); ++j)
                for (int i = std::max(y0, Y); i < std::min({y0 + h, Y + S, j + 1}); ++i) {
                    const std::size_t c = std::size_t(j - x0) * h + (i - y0);
                    const int k = raw[c] | raw[cells + c] << 8 | raw[2 * cells + c] << 16;
                    const float l = std::log2(std::max(float(k), .03f) / std::max(j, 1));
                    const int a = int(std::min(1.f, std::max(0.f, (l - lo) / (hi - lo))) * (L - 1));
                    for (int m = 0; m < 3; ++m) {
                        const auto v = (unsigned char)(255 * lut[3 * a + m]);
                        img[3 * (std::size_t(S - 1 - (i - Y)) * S + (j - X)) + m] = v;
                        if (X == Y) img[3 * (std::size_t(S - 1 - (j - Y)) * S + (i - X)) + m] = v;
                    }
                }
        }
        std::fclose(f);
    }
    std::FILE* f = std::fopen(argv[7], "wb");
    if (!f) { std::perror(argv[7]); return 1; }
    std::fprintf(f, "P6\n%d %d\n255\n", S, S);
    const bool ok = std::fwrite(img.data(), 1, img.size(), f) == img.size();
    return std::fclose(f) == 0 && ok ? 0 : 1;
}
