// stone_tri: cuts by the planes x+y+z = s, seen face-on and scaled by 1/s so the
// triangle x,y,z >= 0 always has the same size. All 6 orders of every P-position.
// BINS equal bins of s; writes BINS frames of PIXELS^2 float counts to OUT and the
// largest s (in units of N) to OUT.smax.
// Usage: stone_tri table.bin N PIXELS BINS cube|full OUT
//   cube: only x,y,z < N, up to the largest s.  full: every position with s < 2N
//   (the table is complete there, also outside the cube).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
int main(int c, char** v)
{
    if (c != 7) { std::fprintf(stderr, "usage: stone_tri table.bin N PIXELS BINS cube|full OUT\n"); return 2; }
    const int n = std::atoi(v[2]), P = std::atoi(v[3]), F = std::atoi(v[4]);
    const bool cube = std::string(v[5]) == "cube";
    std::vector<std::int32_t> T(std::size_t(n) * n);
    std::FILE* f = std::fopen(v[1], "rb");
    if (!f || std::fread(T.data(), 4, T.size(), f) != T.size()) { std::perror(v[1]); return 1; }
    std::fclose(f);
    auto use = [&](int k, long s) { return cube ? k < n : s < 2L * n; };
    long smax = 0;
#pragma omp parallel for reduction(max : smax)
    for (int i = 0; i < n; ++i)
        for (int j = i; j < n; ++j) {
            const int k = T[std::size_t(i) * n + j];
            const long s = long(i) + j + k;
            if (k >= j && use(k, s)) smax = std::max(smax, s);
        }
    const std::size_t plane = std::size_t(P) * P;
    std::vector<float> img(plane * F);
    const double sc = .94 * P / std::sqrt(2.), s2 = 1 / std::sqrt(2.), s6 = 1 / std::sqrt(6.);
#pragma omp parallel for schedule(dynamic, 16)
    for (int i = 0; i < n; ++i)
        for (int j = i; j < n; ++j) {
            const int k = T[std::size_t(i) * n + j];
            const long s = long(i) + j + k;
            if (k < j || s == 0 || !use(k, s)) continue;
            float* frame = &img[plane * (s * F / (smax + 1))];
            const double p[3] = {double(i) / s, double(j) / s, double(k) / s};
            static const int o[6][3] = {{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
            for (auto& t : o) {
                const int px = int(P / 2. + sc * (p[t[1]] - p[t[0]]) * s2);
                const int py = int(P / 2. - sc * ((2 * p[t[2]] - p[t[0]] - p[t[1]]) * s6 - .5 * s6)); // centred
                if (px < 0 || py < 0 || px >= P || py >= P) continue;
#pragma omp atomic
                frame[std::size_t(py) * P + px] += 1;
            }
        }
    f = std::fopen(v[6], "wb");
    const bool ok = f && std::fwrite(img.data(), 4, img.size(), f) == img.size();
    if (!f || std::fclose(f) != 0 || !ok) { std::perror(v[6]); return 1; }
    f = std::fopen((std::string(v[6]) + ".smax").c_str(), "w");
    if (!f) return 1;
    std::fprintf(f, "%.6f\n", double(smax + 1) / n);
    return std::fclose(f) != 0;
}
