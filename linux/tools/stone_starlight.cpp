// stone_starlight: like stone_star, but adds light: every P-position is a star
// of brightness 1/r^2 (r = distance from (0,0,0)), red near, green in the middle,
// blue far, blending linearly in r. Writes PIXELS^2 x 3 doubles (R,G,B), and
// out-ball with only r <= sqrt(2)*N (complete in every direction).
// Usage: stone_starlight DIR N H PIXELS out     Build: -fopenmp -lzstd
#include <zstd.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc != 6) { std::fprintf(stderr, "usage: stone_star DIR N H PIXELS out.u32\n"); return 2; }
    const std::string dir = argv[1];
    const int n = std::atoi(argv[2]), H = std::atoi(argv[3]), P = std::atoi(argv[4]);
    int B = std::min(64, H);
    while (H % B) --B;
    const double zoom = 0.68; // plane half-width
    const double s2 = 1 / std::sqrt(2.), s3 = 1 / std::sqrt(3.), s6 = 1 / std::sqrt(6.);
    std::vector<double> img(std::size_t(P) * P * 3), bimg(img.size());
    const double ball = std::sqrt(2.) * n;
    // Largest possible r: i = j = n-1, k < 2n+64 gives sqrt(6)*n.
    const double rmax = std::sqrt(6.) * n;
    auto light = [](double r, double R, double* c) {
        const double t = std::min(1., r / R), w = R / r * (R / r);
        c[0] = w * std::max(0., 1 - 2 * t);
        c[1] = w * (1 - std::fabs(2 * t - 1));
        c[2] = w * std::max(0., 2 * t - 1);
    };
    std::uint64_t points = 0;
    for (int y0 = 0; y0 < n; y0 += H) {
        const int h = std::min(H, n - y0), groups = (n - y0 + B - 1) / B;
        std::FILE* f = std::fopen((dir + "/out_" + std::to_string(y0) + ".zst").c_str(), "rb");
        if (!f) { std::perror("open strip"); return 1; }
        bool bad = false;
#pragma omp parallel for schedule(dynamic, 1) ordered reduction(+ : points) reduction(|| : bad)
        for (int g = 0; g < groups; ++g) {
            std::uint64_t hd[2] = {0, 0};
            std::vector<char> z;
            bool ok = true;
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
                    if (k < j || k == 0) continue; // each triple once
                    ++points;
                    const int i = y0 + r;
                    const double q = 1 / std::sqrt(double(i) * i + double(j) * j + double(k) * k);
                    const double dd = 1 / q;
                    double ca[3], cb[3];
                    light(dd, rmax, ca);
                    light(dd, ball, cb);
                    const double p[3] = {i * q, j * q, k * q};
                    const double m = std::sqrt(2 / (1 + (p[0] + p[1] + p[2]) * s3)) / zoom;
                    static const int o[6][3] = {{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
                    for (auto& t : o) {
                        const double X = (p[t[1]] - p[t[0]]) * s2;
                        const double Y = (2 * p[t[2]] - p[t[0]] - p[t[1]]) * s6;
                        const int px = int(std::floor((1 + X * m) * P / 2)), py = int(std::floor((1 - Y * m) * P / 2));
                        if (px >= 0 && py >= 0 && px < P && py < P) {
const std::size_t at = std::size_t(py) * P + px;
for (int c = 0; c < 3; ++c) {
#pragma omp atomic
                                img[at * 3 + c] += ca[c];
                            }
                            if (dd <= ball)
                                for (int c = 0; c < 3; ++c) {
#pragma omp atomic
                                    bimg[at * 3 + c] += cb[c];
                                }
                        }
                    }
                }
        }
        std::fclose(f);
        if (bad) { std::fprintf(stderr, "strip %d: truncated or corrupt\n", y0); return 1; }
        std::fprintf(stderr, "strip %d done, %llu triples\n", y0, (unsigned long long)points);
    }
    auto save = [](const std::string& path, const void* p, std::size_t bytes) {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        const bool ok = f && std::fwrite(p, 1, bytes, f) == bytes;
        if (!f || std::fclose(f) != 0 || !ok) { std::perror(path.c_str()); std::exit(1); }
    };
    const std::string o = argv[5];
    save(o, img.data(), img.size() * 8);
    save(o + "-ball", bimg.data(), bimg.size() * 8);
    return 0;
}
