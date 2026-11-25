// stone: computes f(i,j) for i,j < dim, where (i, j, f(i,j)) are the losing
// (P-)positions of the 3-pile game in which a move removes t > 0 tokens from
// one pile, or the same t from any two piles, or the same t from all three.
// Equivalently f(i,j) is the smallest k not excluded by earlier cells in its
// row, column and diagonal. Writes dim*dim little-endian int32, row-major.
//
// Usage: stone [dim] [output-file]     (defaults: 3000, res.bin)
//
// Algorithm (exact; output identical to the straightforward version):
//
// * Exclusion sets without shifting. An earlier cell at distance t with value
//   v excludes v (one-pile move along the line) and v + t (two/three-pile
//   move). Instead of shifting a bitset every step, store the key v - pos
//   ("u" sets); cell value x is then excluded iff x - pos(cell) is a key.
//
// * Symmetry. The game is symmetric in the three piles, so f(i,j) = k implies
//   every permutation of (i,j,k) is a P-position: f(j,i)=k, f(i,k)=j,
//   f(j,k)=i. Only the upper half i <= j is processed. For a triple
//   a <= b <= c, cell (a,b) is processed first, so it is the only one that
//   has to be searched; it fills in (a,c) and (b,c) for later.
//   If cell (i,j), i <= j, was not filled in, its value is >= j, so the
//   search starts at bit j.
//
// * Column j's exclusion set at the time (i,j) is processed equals row j's
//   (mirror cells), and diagonal d equals diagonal -d, so only rows and
//   non-negative diagonals are stored.
//
// * Cells are processed in cache-sized tiles in wavefront order; tiles on one
//   wavefront are processed in parallel (OpenMP). Tile size: env STONE_TILE
//   (default 64).

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

using u64 = std::uint64_t;

// Capacity is 2*dim + 64. f(i,j) <= 2*max(i,j) is NOT always true (e.g.
// f(14,15) = 41), but up to dim 16000 it is exceeded by at most 16. The solver
// stops with an error if the capacity is reached (provable: 2*max + 4*min).
constexpr int kCapFactor = 2;

class Solver {
public:
    explicit Solver(int n)
        : n_(n),
          cap_bits_(std::size_t(kCapFactor) * n + 64),
          wd_(cap_bits_ / 64 + 2),            // plain sets: values [0, cap)
          wu_((cap_bits_ + n) / 64 + 2),      // key sets: v - pos + n in [0, cap + n)
          rd_(std::size_t(n) * wd_, 0), ru_(std::size_t(n) * wu_, 0),
          dd_(std::size_t(n) * wd_, 0), du_(std::size_t(n) * wu_, 0),
          f_(std::size_t(n) * n, -1)
    {
    }

    // Cells are processed in B x B tiles (upper triangle only). Tiles are
    // visited in wavefront order of their tile coordinates (I + J), and cells
    // inside a tile row by row. Every cell's row/column/diagonal predecessors
    // and every symmetric fill-in source lie in an earlier tile or earlier in
    // the same tile, and tiles on one tile-diagonal touch disjoint rows and
    // diagonals, so they run in parallel. Tiling keeps the bitset windows a
    // tile scans in cache instead of streaming everything per anti-diagonal.
    bool run(int tile)
    {
        const int tiles = (n_ + tile - 1) / tile;
        const int waves = 2 * tiles - 1;
        int last_pct = -1;
        bool ok = true;
        for (int S = 0; S < waves && ok; ++S) {
            const int lo = std::max(0, S - (tiles - 1));
            const int hi = S / 2; // I <= J
#pragma omp parallel for schedule(dynamic, 1) reduction(&& : ok)
            for (int I = lo; I <= hi; ++I) {
                const int J = S - I;
                const int i1 = std::min(n_, (I + 1) * tile);
                const int j0 = J * tile, j1 = std::min(n_, (J + 1) * tile);
                for (int i = I * tile; i < i1 && ok; ++i)
                    for (int j = std::max(i, j0); j < j1 && ok; ++j)
                        ok = cell(i, j);
            }
            const int pct = int(100LL * (S + 1) * (S + 1) / (long long)waves / waves);
            if (pct != last_pct) {
                std::cerr << "\r" << pct << "%" << std::flush;
                last_pct = pct;
            }
        }
        std::cerr << "\n";
        return ok;
    }

    const std::vector<std::int32_t>& table() const { return f_; }

private:
    static bool get(const u64* a, std::size_t bit) { return (a[bit >> 6] >> (bit & 63)) & 1; }
    static void set(u64* a, std::size_t bit) { a[bit >> 6] |= u64{1} << (bit & 63); }
    // 64 bits of a starting at bit p.
    static u64 window(const u64* a, std::size_t p)
    {
        const std::size_t w = p >> 6;
        const unsigned b = p & 63;
        return b ? (a[w] >> b) | (a[w + 1] << (64 - b)) : a[w];
    }

    u64* rd(int r) { return &rd_[std::size_t(r) * wd_]; }
    u64* ru(int r) { return &ru_[std::size_t(r) * wu_]; }
    u64* dd(int d) { return &dd_[std::size_t(d) * wd_]; }
    u64* du(int d) { return &du_[std::size_t(d) * wu_]; }
    std::int32_t& at(int i, int j) { return f_[std::size_t(i) * n_ + j]; }

    // Cell (i,j), i <= j. Returns false if the capacity bound is exceeded.
    bool cell(int i, int j)
    {
        const int d = j - i;
        u64 *Ri = rd(i), *Rj = rd(j), *D = dd(d);
        u64 *Ui = ru(i), *Uj = ru(j), *E = du(d);

        long k = at(i, j);
        if (k < 0) {
            // Search for the smallest non-excluded value >= j.
            // Row i (cells (i,j'), j'<j): values, and keys v - j'.
            // Column j == row j (cells (j,i'), i'<i): values, and keys v - i'.
            // Diagonal d (cells (i-t,j-t)): values, and keys v - (i-t).
            const std::size_t oj = std::size_t(n_) - j, oi = std::size_t(n_) - i;
            std::size_t t = std::size_t(j) >> 6;
            u64 low = (u64{1} << (j & 63)) - 1; // bits below j: treat as taken
            for (;; ++t, low = 0) {
                const std::size_t base = t * 64;
                if (base >= cap_bits_) {
                    report_cap(i, j);
                    return false;
                }
                const u64 taken = low | Ri[t] | Rj[t] | D[t] |
                                  window(Ui, base + oj) | window(Uj, base + oi) |
                                  window(E, base + oi);
                if (~taken) {
                    k = long(base) + __builtin_ctzll(~taken);
                    break;
                }
            }
            if (k >= long(cap_bits_)) {
                report_cap(i, j);
                return false;
            }
            // Fill in the other cells of the triple {i, j, k}, i <= j <= k.
            if (k < n_) {
                at(i, k) = at(k, i) = j;
                at(j, k) = at(k, j) = i;
            }
        }
        at(i, j) = at(j, i) = std::int32_t(k);

        // Record the cell (and its mirror (j,i)) in its row, column, diagonal.
        set(Ri, k);
        set(Rj, k);
        set(D, k);
        set(Ui, k - j + n_); // row i:      key v - j'
        set(Uj, k - i + n_); // row j (=column j for later cells): key v - i'
        set(E, k - i + n_);  // diagonal:   key v - min(row, col)
        return true;
    }

    void report_cap(int i, int j)
    {
#pragma omp critical
        std::cerr << "\nerror: f(" << i << "," << j << ") exceeds " << kCapFactor
                  << "*dim; raise kCapFactor in stone.cpp\n";
    }

    int n_;
    std::size_t cap_bits_, wd_, wu_;
    std::vector<u64> rd_, ru_, dd_, du_;
    std::vector<std::int32_t> f_;
};

} // namespace

int main(int argc, char* argv[])
{
    int dim = 3000;
    std::string out_path = "res.bin";
    if (argc > 1) dim = std::atoi(argv[1]);
    if (argc > 2) out_path = argv[2];
    if (dim <= 0 || argc > 3) {
        std::cerr << "usage: " << argv[0] << " [dim>0] [output-file]\n";
        return 2;
    }

    int tile = 64;
    if (const char* t = std::getenv("STONE_TILE")) tile = std::max(1, std::atoi(t));

    Solver solver(dim);
    if (!solver.run(tile))
        return 1;

    std::FILE* f = std::fopen(out_path.c_str(), "wb");
    if (!f) {
        std::perror(out_path.c_str());
        return 1;
    }
    const auto& t = solver.table();
    const bool ok = std::fwrite(t.data(), sizeof(std::int32_t), t.size(), f) == t.size();
    if (std::fclose(f) != 0 || !ok) {
        std::cerr << "error writing " << out_path << "\n";
        return 1;
    }
    std::cerr << "wrote " << out_path << " (" << dim << "x" << dim << " int32)\n";
    return 0;
}
