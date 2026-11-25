// stone_diag: like stone_bulbs, but cuts the cube by planes perpendicular to (1,1,1):
// COUNT equal slices of x+y+z from 0 to the largest value in the cube, in one pass.
// Colours span the whole cube. Writes OUTDIR/0..COUNT-1 (PIXELS^2 x 3 doubles) and
// OUTDIR/smax (largest x+y+z, in cube sides).
// Usage: stone_diag table.bin N PIXELS EX EY EZ FOV_DEG OUTDIR COUNT
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
int main(int c, char** v)
{
    if (c != 10) { std::fprintf(stderr, "usage: stone_diag table.bin N PIXELS EX EY EZ FOV OUTDIR COUNT\n"); return 2; }
    const int n = std::atoi(v[2]), P = std::atoi(v[3]);
    const double e[3] = {std::atof(v[4]), std::atof(v[5]), std::atof(v[6])};
    std::vector<std::int32_t> T(std::size_t(n) * n);
    std::FILE* f = std::fopen(v[1], "rb");
    if (!f || std::fread(T.data(), 4, T.size(), f) != T.size()) { std::perror(v[1]); return 1; }
    std::fclose(f);
    double w[3], m = 0;
    for (int a = 0; a < 3; ++a) { w[a] = .5 - e[a]; m += w[a] * w[a]; }
    for (double& x : w) x /= std::sqrt(m);
    const double hl = std::hypot(w[0], w[1]);
    const double r[3] = {w[1] / hl, -w[0] / hl, 0}; // right = w x z
    const double u[3] = {-r[1] * 0 + r[1] * w[2] - 0, -(r[0] * w[2]), r[0] * w[1] - r[1] * w[0]}; // up = r x w
    const double fl = P / 2. / std::tan(std::atof(v[7]) * M_PI / 360);
    double dn = 0, df = 0; // nearest and farthest distance of the cube
    for (int a = 0; a < 3; ++a) {
        const double l = 0, h = 1;
        const double q = std::max({0., l - e[a], e[a] - h}), g = std::max(std::fabs(e[a] - l), std::fabs(h - e[a]));
        dn += q * q; df += g * g;
    }
    dn = std::sqrt(dn); df = std::sqrt(df);
    const int count = std::atoi(v[9]);
    const std::size_t plane = std::size_t(P) * P * 3;
    long smax = 0;
#pragma omp parallel for reduction(max : smax)
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            const int k = T[std::size_t(i) * n + j];
            if (k < n) smax = std::max(smax, long(i) + j + k);
        }
    std::vector<float> img(plane * count);
#pragma omp parallel for schedule(dynamic, 16)
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            const int k = T[std::size_t(i) * n + j];
            if (k >= n) continue;
            const double p[3] = {double(i) / n - e[0], double(j) / n - e[1], double(k) / n - e[2]};
            const double z = p[0] * w[0] + p[1] * w[1] + p[2] * w[2];
            if (z < 1e-3) continue;
            const double X = p[0] * r[0] + p[1] * r[1], Y = p[0] * u[0] + p[1] * u[1] + p[2] * u[2];
            const int px = int(std::floor(P / 2. + fl * X / z)), py = int(std::floor(P / 2. - fl * Y / z));
            if (px < 0 || py < 0 || px >= P || py >= P) continue;
            const double d2 = p[0] * p[0] + p[1] * p[1] + p[2] * p[2], t = (std::sqrt(d2) - dn) / (df - dn), g = 1 / d2;
            const float col[3] = {float(g * std::max(0., 1 - 2 * t)), float(g * (1 - std::fabs(2 * t - 1))), float(g * std::max(0., 2 * t - 1))};
            for (int a = 0; a < 3; ++a) {
#pragma omp atomic
                img[plane * ((long(i) + j + k) * count / (smax + 1)) + (std::size_t(py) * P + px) * 3 + a] += col[a];
            }
        }
    for (int q = 0; q < count; ++q) {
        const std::vector<double> out(img.begin() + plane * q, img.begin() + plane * (q + 1));
        f = std::fopen((std::string(v[8]) + "/" + std::to_string(q)).c_str(), "wb");
        const bool ok = f && std::fwrite(out.data(), 8, out.size(), f) == out.size();
        if (!f || std::fclose(f) != 0 || !ok) { std::perror("write"); return 1; }
    }
    f = std::fopen((std::string(v[8]) + "/smax").c_str(), "w");
    if (!f) return 1;
    std::fprintf(f, "%.6f\n", double(smax + 1) / n);
    return std::fclose(f) != 0;
}
