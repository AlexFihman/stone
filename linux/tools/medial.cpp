// Check mediality f(f(a,b), f(c,d)) == f(f(a,c), f(b,d)) for all a,b,c,d < m,
// using a dim x dim table written by stone.
// Usage: medial <table.bin> <dim> <m> [max-examples]
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 4) { std::fprintf(stderr, "usage: medial table.bin dim m [max-examples]\n"); return 2; }
    const int n = std::atoi(argv[2]), m = std::atoi(argv[3]);
    const std::size_t keep = argc > 4 ? std::atoi(argv[4]) : 10;
    std::vector<int32_t> F(std::size_t(n) * n);
    std::FILE* fp = std::fopen(argv[1], "rb");
    if (!fp || std::fread(F.data(), 4, F.size(), fp) != F.size()) { std::perror(argv[1]); return 1; }
    std::fclose(fp);
    auto f = [&](int x, int y) { return F[std::size_t(x) * n + y]; };

    // Every inner value must itself be a valid index into the table.
    int inner_max = 0;
    for (int x = 0; x < m; ++x)
        for (int y = 0; y < m; ++y) inner_max = std::max(inner_max, f(x, y));
    std::printf("max f(x,y) for x,y < %d: %d (table dim %d)\n", m, inner_max, n);
    if (inner_max >= n) { std::printf("inner values leave the grid; aborting\n"); return 1; }

    using Ex = std::array<int, 7>; // max, a, b, c, d, lhs, rhs
    long long bad = 0;
    std::vector<Ex> examples;
#pragma omp parallel
    {
        long long lb = 0;
        std::vector<Ex> lex;
#pragma omp for schedule(dynamic, 1)
        for (int a = 0; a < m; ++a)
            for (int b = 0; b < m; ++b) {
                const int ab = f(a, b);
                for (int c = 0; c < m; ++c) {
                    const int ac = f(a, c);
                    for (int d = 0; d < m; ++d) {
                        const int lhs = f(ab, f(c, d)), rhs = f(ac, f(b, d));
                        if (lhs != rhs) {
                            ++lb;
                            lex.push_back({std::max({a, b, c, d}), a, b, c, d, lhs, rhs});
                            if (lex.size() > 4 * keep) { std::sort(lex.begin(), lex.end()); lex.resize(keep); }
                        }
                    }
                }
            }
#pragma omp critical
        { bad += lb; examples.insert(examples.end(), lex.begin(), lex.end()); }
    }
    std::sort(examples.begin(), examples.end());
    if (examples.size() > keep) examples.resize(keep);
    const double total = double(m) * m * m * m;
    std::printf("quadruples=%.0f  violations=%lld (%.4f%%)\n", total, bad, 100.0 * bad / total);
    for (auto& e : examples)
        std::printf("  a=%d b=%d c=%d d=%d: f(f(a,b),f(c,d))=%d  f(f(a,c),f(b,d))=%d\n",
                    e[1], e[2], e[3], e[4], e[5], e[6]);
    return bad ? 3 : 0;
}
