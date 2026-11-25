// stone_cuda: GPU version of stone (same algorithm and output, see stone.cpp).
//
// One kernel launch per anti-diagonal s = i + j (cells i <= j). Cells on one
// anti-diagonal touch disjoint rows and diagonals, so they run concurrently.
// Each cell is handled by one warp: the 32 lanes test 32 consecutive 64-bit
// words of the exclusion sets at once and a warp vote picks the first free
// bit. Only the upper triangle of the table is kept on the device.
//
// Usage: stone_cuda [dim] [output-file]     (defaults: 3000, res.bin)

#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using u64 = unsigned long long;

constexpr int kCapFactor = 2; // capacity 2*dim + 64, see stone.cpp (checked at run time)
constexpr int kWarpsPerBlock = 8;

#define CUDA_CHECK(x)                                                              \
    do {                                                                           \
        cudaError_t e_ = (x);                                                      \
        if (e_ != cudaSuccess) {                                                   \
            std::fprintf(stderr, "CUDA error %s at %s:%d: %s\n", #x, __FILE__,     \
                         __LINE__, cudaGetErrorString(e_));                        \
            std::exit(1);                                                          \
        }                                                                          \
    } while (0)

struct Dev {
    int n;
    u64 cap_bits;        // values are < cap_bits
    u64 wd, wu;          // words per line: plain sets, key sets
    u64 *rd, *ru, *dd, *du;
    int* tri;            // upper triangle, row-major, -1 = unknown
    int* error;          // set if the capacity bound is exceeded
};

__host__ __device__ inline u64 tri_index(u64 n, u64 i, u64 j)
{
    return i * n - i * (i - 1) / 2 + (j - i); // i <= j
}

__device__ inline u64 window(const u64* a, u64 p)
{
    const u64 w = p >> 6;
    const unsigned b = p & 63;
    return b ? (a[w] >> b) | (a[w + 1] << (64 - b)) : a[w];
}

__device__ inline void set_bit(u64* a, u64 bit) { a[bit >> 6] |= 1ull << (bit & 63); }

__global__ void diagonal(Dev d, int s, int lo, int count)
{
    const int warp = (blockIdx.x * blockDim.x + threadIdx.x) >> 5;
    const int lane = threadIdx.x & 31;
    if (warp >= count) return;

    const int i = lo + warp, j = s - i, dg = j - i;
    const u64 n = d.n;
    u64* Ri = d.rd + u64(i) * d.wd;
    u64* Rj = d.rd + u64(j) * d.wd;
    u64* D = d.dd + u64(dg) * d.wd;
    u64* Ui = d.ru + u64(i) * d.wu;
    u64* Uj = d.ru + u64(j) * d.wu;
    u64* E = d.du + u64(dg) * d.wu;

    long long k = d.tri[tri_index(n, i, j)];
    if (k < 0) {
        // Smallest value >= j not excluded by row i, row j (= column j) or
        // diagonal dg; see stone.cpp for the key encoding.
        const u64 oj = n - j, oi = n - i;
        const u64 t0 = u64(j) >> 6;
        for (u64 chunk = t0;; chunk += 32) {
            const u64 t = chunk + lane;
            const u64 base = t * 64;
            u64 taken = 0;
            if (base < d.cap_bits)
                taken = Ri[t] | Rj[t] | D[t] | window(Ui, base + oj) |
                        window(Uj, base + oi) | window(E, base + oi);
            if (t == t0) taken |= (1ull << (j & 63)) - 1;
            const unsigned free_lanes = __ballot_sync(0xffffffffu, ~taken != 0);
            if (free_lanes) {
                const int src = __ffs(free_lanes) - 1;
                const u64 tk = __shfl_sync(0xffffffffu, taken, src);
                k = (long long)((chunk + src) * 64) + __ffsll((long long)~tk) - 1;
                break;
            }
        }
        if (u64(k) >= d.cap_bits) {
            if (lane == 0) atomicExch(d.error, 1);
            return;
        }
        // Fill in the other cells of the triple {i, j, k}, i <= j <= k.
        if (lane == 0 && k < (long long)n) {
            d.tri[tri_index(n, i, k)] = j;
            d.tri[tri_index(n, j, k)] = i;
        }
    }
    if (lane == 0) {
        d.tri[tri_index(n, i, j)] = int(k);
        set_bit(Ri, k);
        set_bit(Rj, k);
        set_bit(D, k);
        set_bit(Ui, k - j + n);
        set_bit(Uj, k - i + n);
        set_bit(E, k - i + n);
    }
}

double seconds_since(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace

int main(int argc, char* argv[])
{
    int dim = 3000;
    std::string out_path = "res.bin";
    if (argc > 1) dim = std::atoi(argv[1]);
    if (argc > 2) out_path = argv[2];
    if (dim <= 0 || argc > 3) {
        std::fprintf(stderr, "usage: %s [dim>0] [output-file]\n", argv[0]);
        return 2;
    }
    const auto t_start = std::chrono::steady_clock::now();

    Dev d{};
    d.n = dim;
    d.cap_bits = u64(kCapFactor) * dim + 64;
    d.wd = d.cap_bits / 64 + 2;
    d.wu = (d.cap_bits + dim) / 64 + 2;
    const u64 n = dim;
    const u64 tri_cells = n * (n + 1) / 2;
    const u64 set_bytes = n * (d.wd + d.wu) * 2 * sizeof(u64);
    const u64 tri_bytes = tri_cells * sizeof(int);

    size_t free_mem = 0, total_mem = 0;
    CUDA_CHECK(cudaMemGetInfo(&free_mem, &total_mem));
    if (set_bytes + tri_bytes + (64 << 20) > free_mem) {
        std::fprintf(stderr, "dim %d needs %.2f GB of GPU memory, %.2f GB free\n", dim,
                     (set_bytes + tri_bytes) / 1e9, free_mem / 1e9);
        return 1;
    }
    CUDA_CHECK(cudaMalloc(&d.rd, n * d.wd * sizeof(u64)));
    CUDA_CHECK(cudaMalloc(&d.dd, n * d.wd * sizeof(u64)));
    CUDA_CHECK(cudaMalloc(&d.ru, n * d.wu * sizeof(u64)));
    CUDA_CHECK(cudaMalloc(&d.du, n * d.wu * sizeof(u64)));
    CUDA_CHECK(cudaMalloc(&d.tri, tri_bytes));
    CUDA_CHECK(cudaMalloc(&d.error, sizeof(int)));
    CUDA_CHECK(cudaMemset(d.rd, 0, n * d.wd * sizeof(u64)));
    CUDA_CHECK(cudaMemset(d.dd, 0, n * d.wd * sizeof(u64)));
    CUDA_CHECK(cudaMemset(d.ru, 0, n * d.wu * sizeof(u64)));
    CUDA_CHECK(cudaMemset(d.du, 0, n * d.wu * sizeof(u64)));
    CUDA_CHECK(cudaMemset(d.tri, 0xff, tri_bytes)); // -1
    CUDA_CHECK(cudaMemset(d.error, 0, sizeof(int)));

    const auto t_compute = std::chrono::steady_clock::now();
    const int diags = 2 * dim - 1;
    int last_pct = -1;
    for (int s = 0; s < diags; ++s) {
        const int lo = std::max(0, s - (dim - 1));
        const int hi = s / 2;
        const int count = hi - lo + 1;
        const int blocks = (count + kWarpsPerBlock - 1) / kWarpsPerBlock;
        diagonal<<<blocks, 32 * kWarpsPerBlock>>>(d, s, lo, count);
        const int pct = int(100LL * (s + 1) * (s + 1) / (long long)diags / diags);
        if (pct != last_pct) {
            CUDA_CHECK(cudaDeviceSynchronize());
            int err = 0;
            CUDA_CHECK(cudaMemcpy(&err, d.error, sizeof(int), cudaMemcpyDeviceToHost));
            if (err) {
                std::fprintf(stderr, "\nerror: a value exceeds %d*dim; raise kCapFactor\n",
                             kCapFactor);
                return 1;
            }
            std::fprintf(stderr, "\r%d%%", pct);
            last_pct = pct;
        }
    }
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    const double compute_s = seconds_since(t_compute);

    // Expand the upper triangle into the full symmetric table.
    std::vector<int> tri(tri_cells);
    CUDA_CHECK(cudaMemcpy(tri.data(), d.tri, tri_bytes, cudaMemcpyDeviceToHost));
    std::vector<std::int32_t> full(n * n);
    for (u64 i = 0; i < n; ++i) {
        const int* row = &tri[tri_index(n, i, i)];
        for (u64 j = i; j < n; ++j)
            full[i * n + j] = full[j * n + i] = row[j - i];
    }

    std::FILE* f = std::fopen(out_path.c_str(), "wb");
    if (!f) {
        std::perror(out_path.c_str());
        return 1;
    }
    const bool ok = std::fwrite(full.data(), sizeof(std::int32_t), full.size(), f) == full.size();
    if (std::fclose(f) != 0 || !ok) {
        std::fprintf(stderr, "error writing %s\n", out_path.c_str());
        return 1;
    }
    std::fprintf(stderr, "\nwrote %s (%dx%d int32); GPU compute %.3fs, total %.3fs\n",
                 out_path.c_str(), dim, dim, compute_s, seconds_since(t_start));
    return 0;
}
