// stone_bulbs: the cube [0,n)^3 seen in perspective; every P-position (x,y,z) is a
// light bulb of brightness 1/d^2 (d = distance from the eye), red near, green in
// the middle, blue far. Looks at the cube's centre, z up. Units: cube side = 1.
// Usage: stone_bulbs table.bin N PIXELS EX EY EZ FOV_DEG out [X0 X1 [full]]
// Writes PIXELS^2 x 3 doubles. X0 X1 (0..1): only the slice X0 <= x < X1; colours span the slice (full: the whole cube).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(int c, char** v)
{
    if (c != 9 && c != 11 && c != 12) { std::fprintf(stderr, "usage: stone_bulbs table.bin N PIXELS EX EY EZ FOV out [X0 X1 [full]]\n"); return 2; }
    const int n = std::atoi(v[2]), P = std::atoi(v[3]);
    const double e[3] = {std::atof(v[4]), std::atof(v[5]), std::atof(v[6])};
    const double lo[3] = {c > 9 ? std::atof(v[9]) : 0., 0, 0}, hi[3] = {c > 9 ? std::atof(v[10]) : 1., 1, 1};
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
        const double l = c > 11 ? 0 : lo[a], h = c > 11 ? 1 : hi[a];
        const double q = std::max({0., l - e[a], e[a] - h}), g = std::max(std::fabs(e[a] - l), std::fabs(h - e[a]));
        dn += q * q; df += g * g;
    }
    dn = std::sqrt(dn); df = std::sqrt(df);
    std::vector<float> img(std::size_t(P) * P * 3);
#pragma omp parallel for schedule(dynamic, 16)
    for (int i = int(std::ceil(lo[0] * n)); i < std::min(n, int(std::ceil(hi[0] * n))); ++i)
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
                img[(std::size_t(py) * P + px) * 3 + a] += col[a];
            }
        }
    std::vector<double> out(img.begin(), img.end());
    f = std::fopen(v[8], "wb");
    const bool ok = f && std::fwrite(out.data(), 8, out.size(), f) == out.size();
    return f && std::fclose(f) == 0 && ok ? 0 : 1;
}
