// Independent check written directly from the game rules: (a,b,c) is a
// P-position iff no move reaches a P-position, where a move removes t > 0
// from one pile, the same t from two piles, or the same t from all three.
// f(i,j) is the unique c with (i,j,c) a P-position. Compares the first m x m
// cells of a table produced by stone.
// Usage: naive <table.bin> <dim> <m>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv)
{
    if (argc != 4) { std::fprintf(stderr, "usage: naive table.bin dim m\n"); return 2; }
    const int n = std::atoi(argv[2]), m = std::atoi(argv[3]);
    std::vector<int32_t> T(std::size_t(n) * n);
    std::FILE* fp = std::fopen(argv[1], "rb");
    if (!fp || std::fread(T.data(), 4, T.size(), fp) != T.size()) { std::perror(argv[1]); return 1; }
    std::fclose(fp);

    std::vector<int> f(std::size_t(m) * m, -1);
    // (a,b,c) with a,b already computed is a P-position iff f(a,b) == c.
    auto isP = [&](int a, int b, int c) { return f[std::size_t(a) * m + b] == c; };
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < m; ++j) {
            // The smallest c from which no move reaches a P-position. Moves that
            // only shrink the third pile lead to (i,j,c') with c' < c, which are
            // not P-positions because every smaller c was rejected.
            for (int c = 0;; ++c) {
                bool reachesP = false;
                for (int t = 1; !reachesP && t <= i; ++t)
                    reachesP = isP(i - t, j, c)                      // pile 1
                            || isP(i - t, j, c - t)                  // piles 1+3
                            || (t <= j && (isP(i - t, j - t, c)      // piles 1+2
                                        || isP(i - t, j - t, c - t))); // all three
                for (int t = 1; !reachesP && t <= j; ++t)
                    reachesP = isP(i, j - t, c)                      // pile 2
                            || isP(i, j - t, c - t);                 // piles 2+3
                if (!reachesP) { f[std::size_t(i) * m + j] = c; break; }
            }
            const int got = T[std::size_t(i) * n + j];
            if (f[std::size_t(i) * m + j] != got) {
                std::printf("MISMATCH at (%d,%d): rules give %d, table has %d\n",
                            i, j, f[std::size_t(i) * m + j], got);
                return 1;
            }
        }
    std::printf("first %dx%d cells match the game rules\n", m, m);
    return 0;
}
