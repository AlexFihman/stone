// stone_height: f(x,y) seen from above, colour = log2(k / max(x,y)), from
// stone_strips output. Cell colours are averaged per pixel; (0,0) bottom left.
// Blue = half of max(x,y), red = twice; below half fades to black (at 1/32).
// Usage: stone_height DIR N H PIXELS out.ppm    (N must be a multiple of PIXELS,
// and N/PIXELS must divide 64)    Build: -O3 -march=native -fopenmp -lzstd
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
    if (argc != 6) { std::fprintf(stderr, "usage: stone_height DIR N H PIXELS out.ppm\n"); return 2; }
    const std::string dir = argv[1];
    const int n = std::atoi(argv[2]), H = std::atoi(argv[3]), P = std::atoi(argv[4]), B = 64;
    if (P <= 0 || n % P || B % (n / P) || H % B || H % (n / P)) { std::fprintf(stderr, "sizes do not divide\n"); return 2; }
    const int q = n / P, L = 4096;
    const float lo = -5, hi = 1; // log2 ratio range of the lookup table
    std::vector<float> lut(3 * L);
    for (int a = 0; a < L; ++a) {
        const float r = lo + (hi - lo) * a / (L - 1), t = std::max(0.f, (r + 1) / 2);
        const float fade = r < -1 ? (r - lo) / (-1 - lo) : 1;
        hsv(.72f * (1 - t), .95f, fade * (.25f + .75f * std::min(1.f, 1.6f * t + .2f)), &lut[3 * a]);
    }
    std::vector<float> img(std::size_t(P) * P * 3);
    std::vector<std::uint32_t> cnt(std::size_t(P) * P);
    for (int y0 = 0; y0 < n; y0 += H) {
        const int h = std::min(H, n - y0), groups = (n - y0 + B - 1) / B;
        std::FILE* f = std::fopen((dir + "/out_" + std::to_string(y0) + ".zst").c_str(), "rb");
        if (!f) { std::perror("open strip"); return 1; }
        bool bad = false;
#pragma omp parallel for schedule(dynamic, 1) ordered reduction(|| : bad)
        for (int g = 0; g < groups; ++g) {
            std::uint64_t hd[2] = {0, 0};
            std::vector<char> z;
            bool ok;
#pragma omp ordered
            {
                ok = std::fread(hd, 1, 16, f) == 16;
                if (ok) { z.resize(hd[1]); ok = std::fread(z.data(), 1, z.size(), f) == z.size(); }
            }
            std::vector<unsigned char> raw(hd[0]);
            if (ok) ok = ZSTD_decompress(raw.data(), raw.size(), z.data(), z.size()) == raw.size();
            if (!ok) { bad = true; continue; }
            const int x0 = y0 + g * B, x1 = std::min(n, x0 + B);
            const std::size_t cells = raw.size() / 3;
            for (int j = x0; j < x1; ++j)
                for (int r = 0; r < h && y0 + r <= j; ++r) {
                    const std::size_t c = std::size_t(j - x0) * h + r;
                    const int k = raw[c] | raw[cells + c] << 8 | raw[2 * cells + c] << 16;
                    const float l = std::log2(std::max(float(k), .03f) / std::max(j, 1));
                    const int a = int(std::min(1.f, std::max(0.f, (l - lo) / (hi - lo))) * (L - 1));
                    const std::size_t at = std::size_t((y0 + r) / q) * P + j / q; // column groups do not share pixels
                    ++cnt[at];
                    for (int m = 0; m < 3; ++m) img[3 * at + m] += lut[3 * a + m];
                }
        }
        std::fclose(f);
        if (bad) { std::fprintf(stderr, "strip %d: truncated or corrupt\n", y0); return 1; }
        std::fprintf(stderr, "strip %d done\n", y0);
    }
    std::FILE* f = std::fopen(argv[5], "wb");
    if (!f) { std::perror(argv[5]); return 1; }
    std::fprintf(f, "P6\n%d %d\n255\n", P, P);
    std::vector<unsigned char> row(3 * P);
    bool ok = true;
    for (int y = P - 1; y >= 0; --y) {
        for (int x = 0; x < P; ++x) {
            const std::size_t at = std::size_t(std::min(x, y)) * P + std::max(x, y);
            for (int m = 0; m < 3; ++m)
                row[3 * x + m] = (unsigned char)(255 * std::min(1.f, img[3 * at + m] / std::max(1u, cnt[at])));
        }
        ok = ok && std::fwrite(row.data(), 1, row.size(), f) == row.size();
    }
    return std::fclose(f) == 0 && ok ? 0 : 1;
}
