// stone_zoom: cuts x+y+z = s seen face-on and scaled by 1/s, from stone_strips output.
// Bins grow by a constant ratio: bin = log(s/S0)/log(RATIO), S0 <= s < 2N (complete
// there). Zoomed on the star; only the right half (x <= y) is stored: BINS frames of
// PIXELS x PIXELS/2 float counts. The number of bins is printed.
// Usage: stone_zoom DIR N H PIXELS S0 RATIO OUT     Build: -O3 -march=native -fopenmp -lzstd
#include <zstd.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc != 8) { std::fprintf(stderr, "usage: stone_zoom DIR N H PIXELS S0 RATIO OUT\n"); return 2; }
    const std::string dir = argv[1];
    const int n = std::atoi(argv[2]), H = std::atoi(argv[3]), P = std::atoi(argv[4]);
    const double s0 = std::atof(argv[5]), lr = std::log(std::atof(argv[6]));
    int B = 64;
    while (H % B) --B;
    // Bin of every s.
    std::vector<int> bin(2 * std::size_t(n), -1);
    for (long s = long(std::ceil(s0)); s < 2L * n; ++s) bin[s] = int(std::log(s / s0) / lr);
    const int F = bin.back() + 1, HW = P / 2;
    const std::size_t plane = std::size_t(P) * HW;
    std::vector<float> img(plane * F);
    const double sc = .95 * P / .874, cy = -.03, s2 = 1 / std::sqrt(2.), s6 = 1 / std::sqrt(6.);
    std::uint64_t points = 0;
    for (int y0 = 0; y0 < n; y0 += H) {
        const int h = std::min(H, n - y0), groups = (n - y0 + B - 1) / B;
        std::FILE* f = std::fopen((dir + "/out_" + std::to_string(y0) + ".zst").c_str(), "rb");
        if (!f) { std::perror("open strip"); return 1; }
        bool bad = false;
#pragma omp parallel for schedule(dynamic, 1) ordered reduction(|| : bad) reduction(+ : points)
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
                    const int i = y0 + r;
                    const long s = long(i) + j + k;
                    if (k < j || s >= 2L * n || bin[s] < 0) continue;
                    ++points;
                    float* frame = &img[plane * bin[s]];
                    const double q = 1. / s, a = i * q, b = j * q, d = k * q;
                    // x <= y: the three choices of z.
                    const double v[3][3] = {{a, b, d}, {a, d, b}, {b, d, a}};
                    for (auto& t : v) {
                        const int px = int(sc * (t[1] - t[0]) * s2);
                        const int py = int(std::floor(P / 2. - sc * ((2 * t[2] - t[0] - t[1]) * s6 - cy)));
                        if (px >= HW || py < 0 || py >= P) continue;
#pragma omp atomic
                        frame[std::size_t(py) * HW + px] += 1;
                    }
                }
        }
        std::fclose(f);
        if (bad) { std::fprintf(stderr, "strip %d: truncated or corrupt\n", y0); return 1; }
        std::fprintf(stderr, "strip %d done\n", y0);
    }
    std::FILE* f = std::fopen(argv[7], "wb");
    const bool ok = f && std::fwrite(img.data(), 4, img.size(), f) == img.size();
    if (!f || std::fclose(f) != 0 || !ok) { std::perror(argv[7]); return 1; }
    std::printf("bins %d triples %llu\n", F, (unsigned long long)points);
    return 0;
}
