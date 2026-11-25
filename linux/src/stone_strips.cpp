// stone_strips: out-of-core solver. Computes the same table as stone.cpp in
// horizontal strips of H rows; memory is O(N*H), everything else is streamed.
//
// Usage: stone_strips run    DIR N H [max-strips]   (resumes if DIR has work)
//        stone_strips export DIR N H out.bin        (full int32 table; needs RAM)
//
// Strip y0 <= i < y1 is swept over x = y0..N-1 (upper triangle only). Leaving
// the strip at the bottom, column x and the diagonal through it are saved as
// four bit arrays (plain values, keys value-position) in a sequential stream
// that the next strip reads front to back. See stone.cpp for the exclusion
// sets, the symmetry argument and the tile ordering.
//
// * Searches start at j, so plain sets keep only values >= the smallest j
//   that can still query them, and keys only >= 0 (columns) / >= d (diagonals).
// * Values k < j are never searched: they are fill-ins. Fill-ins from earlier
//   strips, f(i,r) = k => f(r,k) = i, are recovered by reading the columns
//   r in [y0,y1) of each earlier strip's output (each block is read once).
// * The saved column sets of x in [y0,y1) are the initial row sets.
//
// Files: out_<y0>.zst + .idx (tile columns, column-major, byte-shuffled, one
// zstd frame per B columns), state_<y>.zst (one frame per B records).
// state_<y> is renamed into place only after the strip's output is complete,
// so a killed run resumes at the last finished strip.

#include <zstd.h>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

using u64 = std::uint64_t;
using i64 = std::int64_t;

static double now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
[[noreturn]] static void die(const std::string& m)
{
    std::fprintf(stderr, "\nerror: %s\n", m.c_str());
    std::exit(1);
}

static constexpr int kSlack = 64; // values must stay below 2*j + kSlack (checked)
static constexpr int kBias = 64;  // key bit = key + kBias

struct Stats { double compute = 0, io = 0; // io: time the solver waits for I/O and copies
    u64 st_raw = 0, st_z = 0, out_raw = 0, out_z = 0, read_z = 0; };

static FILE* xopen(const std::string& p, const char* mode)
{
    FILE* f = std::fopen(p.c_str(), mode);
    if (!f) { std::perror(p.c_str()); std::exit(1); }
    std::setvbuf(f, nullptr, _IOFBF, 4 << 20);
    return f;
}
static void finish(FILE* f, const std::string& name)
{
    if (std::fflush(f) || fsync(fileno(f)) || std::fclose(f)) die("write failed: " + name);
}
using Buf = std::vector<char>;

static Buf compress(const Buf& raw)
{
    Buf z(16 + ZSTD_compressBound(raw.size()));
    const size_t n = ZSTD_compress(z.data() + 16, z.size() - 16, raw.data(), raw.size(), 1);
    if (ZSTD_isError(n)) die("zstd compress");
    const u64 h[2] = {raw.size(), n};
    std::memcpy(z.data(), h, 16);
    z.resize(16 + n);
    return z;
}
static bool read_z(FILE* f, Buf& z) // frame including header; false at EOF
{
    u64 h[2];
    const size_t got = std::fread(h, 1, 16, f);
    if (got == 0) return false;
    if (got != 16) die("short read");
    z.resize(16 + h[1]);
    std::memcpy(z.data(), h, 16);
    if (std::fread(z.data() + 16, 1, h[1], f) != h[1]) die("short read");
    return true;
}
static Buf decompress(const Buf& z)
{
    u64 h[2];
    std::memcpy(h, z.data(), 16);
    Buf raw(h[0]);
    const size_t n = ZSTD_decompress(raw.data(), raw.size(), z.data() + 16, h[1]);
    if (ZSTD_isError(n) || n != h[0]) die("corrupt frame");
    return raw;
}

// Writes frames in order; compression runs on worker threads.
class Writer {
public:
    Writer(FILE* f, size_t depth) : f_(f), depth_(depth), t_([this] { loop(); }) {}
    template <class Fn> void add(Fn make_raw) // make_raw() -> Buf, run on a worker
    {
        std::unique_lock<std::mutex> l(m_);
        cv_.wait(l, [&] { return q_.size() < depth_; });
        q_.push_back(std::async(std::launch::async, [make_raw]() mutable { return compress(make_raw()); }));
        cv_.notify_all();
    }
    u64 finish() // returns bytes written
    {
        { std::lock_guard<std::mutex> l(m_); done_ = true; }
        cv_.notify_all();
        t_.join();
        return pos_;
    }
    const std::vector<u64>& index() const { return index_; }

private:
    void loop()
    {
        for (;;) {
            std::unique_lock<std::mutex> l(m_);
            cv_.wait(l, [&] { return done_ || !q_.empty(); });
            if (q_.empty()) return;
            auto& front = q_.front(); // deque: references survive push_back
            l.unlock();
            const Buf z = front.get();
            index_.push_back(pos_);
            if (std::fwrite(z.data(), 1, z.size(), f_) != z.size()) die("write failed (disk full?)");
            pos_ += z.size();
            l.lock();
            q_.pop_front(); // bounds memory: slot is freed only after the write
            cv_.notify_all();
        }
    }
    FILE* f_;
    size_t depth_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::future<Buf>> q_;
    std::vector<u64> index_;
    u64 pos_ = 0;
    bool done_ = false;
    std::thread t_;
};

// Reads frames ahead in order; decompression runs on worker threads.
class Reader {
public:
    Reader(FILE* f, size_t depth) : f_(f), depth_(depth), t_([this] { loop(); }) {}
    ~Reader()
    {
        { std::lock_guard<std::mutex> l(m_); stop_ = true; }
        cv_.notify_all();
        t_.join();
    }
    Buf next()
    {
        std::unique_lock<std::mutex> l(m_);
        cv_.wait(l, [&] { return eof_ || !q_.empty(); });
        if (q_.empty()) die("state file too short");
        auto fut = std::move(q_.front());
        q_.pop_front();
        cv_.notify_all();
        l.unlock();
        return fut.get();
    }
    u64 bytes() const { return bytes_; }

private:
    void loop()
    {
        for (;;) {
            {
                std::unique_lock<std::mutex> l(m_);
                cv_.wait(l, [&] { return stop_ || q_.size() < depth_; });
                if (stop_) return;
            }
            Buf z;
            const bool more = read_z(f_, z);
            std::lock_guard<std::mutex> l(m_);
            if (!more) { eof_ = true; cv_.notify_all(); return; }
            bytes_ += z.size();
            q_.push_back(std::async(std::launch::async, [z = std::move(z)] { return decompress(z); }));
            cv_.notify_all();
        }
    }
    FILE* f_;
    size_t depth_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::future<Buf>> q_;
    u64 bytes_ = 0;
    bool eof_ = false, stop_ = false;
    std::thread t_;
};

// Table cells: 3 bytes, kNone = unknown.
static constexpr std::uint32_t kNone = 0xffffff;
static inline std::uint32_t load3(const unsigned char* p) { return p[0] | p[1] << 8 | std::uint32_t(p[2]) << 16; }
static inline void store3(unsigned char* p, std::uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; }

static Buf shuffle3(const unsigned char* in, size_t n)
{
    Buf out(3 * n);
    for (size_t i = 0; i < n; ++i)
        for (size_t b = 0; b < 3; ++b) out[b * n + i] = char(in[3 * i + b]);
    return out;
}
static inline std::uint32_t shuffled(const Buf& in, size_t n, size_t i)
{
    return (unsigned char)in[i] | (unsigned char)in[n + i] << 8 | std::uint32_t((unsigned char)in[2 * n + i]) << 16;
}

static inline void set_bit(u64* a, i64 bit) { a[bit >> 6] |= u64{1} << (bit & 63); }

class Strips {
public:
    Strips(std::string dir, int n, int h) : dir_(std::move(dir)), N(n), H(h)
    {
        if (N >= (1 << 22)) die("N must be below 2^22");
        B = std::min(64, H);
        while (H % B) --B;
        // Words per array, from the value range each can hold or be asked for
        // (values < 2j + kSlack). kPad covers the 8-word scan blocks.
        const size_t n1 = size_t(N), hh = size_t(H);
        Wrp = words(2 * n1);        // rows: values, from y0
        Wrk = words(2 * n1);        // rows: keys (up to 2*y1 in the triangle part)
        Wcp = words(n1);            // columns: values x..2x
        Wck = words(2 * n1);        // columns: keys
        Wdp = words(n1 + hh);       // diagonals: values from y0+d
        Wdk = words(n1);            // diagonals: keys, v - j
        const std::string p = dir_ + "/params";
        if (FILE* f = std::fopen(p.c_str(), "r")) {
            int a = 0, b = 0;
            if (std::fscanf(f, "%d %d", &a, &b) != 2 || a != N || b != H) die("directory was created with other N/H");
            std::fclose(f);
        } else {
            f = xopen(p, "w");
            std::fprintf(f, "%d %d\n", N, H);
            finish(f, p);
        }
    }
    std::string out_name(int y) const { return dir_ + "/out_" + std::to_string(y) + ".zst"; }
    std::string state_name(int y) const { return dir_ + "/state_" + std::to_string(y) + ".zst"; }

    int first_unfinished() const
    {
        int y = 0;
        for (int c = 0; c < N; c += H)
            if (access(state_name(std::min(N, c + H)).c_str(), F_OK) == 0) y = std::min(N, c + H);
        return y;
    }

    void run(int max_strips)
    {
        const double need = 8.0 * H * (Wrp + Wrk + Wcp + Wck + 2.0 * (Wdp + Wdk)) + 3.0 * N * double(H);
        const double frames = 2.0 * (kDepth + 2) * B * 8.0 * (Wcp + Wck + Wdp + Wdk);
        double avail = 0;
        if (FILE* f = std::fopen("/proc/meminfo", "r")) {
            char line[256];
            unsigned long long kb;
            while (std::fgets(line, sizeof line, f))
                if (std::sscanf(line, "MemAvailable: %llu", &kb) == 1) avail = kb * 1024.0;
            std::fclose(f);
        }
        std::fprintf(stderr, "memory: %.2f GB arrays + up to %.2f GB I/O buffers, %.2f GB available\n", need / 1e9,
                     frames / 1e9, avail / 1e9);
        if (avail > 0 && need + frames > avail) die("not enough memory for this H");
        for (int y = first_unfinished(); y < N && max_strips-- > 0; y = std::min(N, y + H)) strip(y);
    }

    void export_full(const std::string& path)
    {
        std::vector<std::int32_t> full(size_t(N) * N);
        Buf z;
        for (int y0 = 0; y0 < N; y0 += H) {
            const int h = std::min(H, N - y0);
            FILE* f = xopen(out_name(y0), "rb");
            for (int x0 = y0; x0 < N; x0 += B) {
                if (!read_z(f, z)) die("output file too short");
                const Buf raw = decompress(z);
                const size_t cells = raw.size() / 3;
                for (int x = x0; x < std::min(N, x0 + B); ++x)
                    for (int r = 0; r < h && y0 + r <= x; ++r) {
                        const auto k = std::int32_t(shuffled(raw, cells, size_t(x - x0) * h + r));
                        full[size_t(y0 + r) * N + x] = full[size_t(x) * N + y0 + r] = k;
                    }
            }
            std::fclose(f);
        }
        FILE* f = xopen(path, "wb");
        if (std::fwrite(full.data(), 4, full.size(), f) != full.size()) die("write failed");
        finish(f, path);
    }

private:
    std::string dir_;
    int N, H, B;
    size_t Wrp, Wrk, Wcp, Wck, Wdp, Wdk; // words per bit array
    static constexpr size_t kPad = 16;
    static size_t words(size_t bits) { return (bits + kSlack + kBias + 64) / 64 + kPad; }

    // Strip-local state.
    int y0, y1, h, T, M;
    std::vector<u64> rp, rk, cp, ck, dp, dk;
    std::vector<unsigned char> tab; // 3-byte cells, column-major: (x-y0)*h + (r-y0)

    unsigned char* at(int r, int x) { return &tab[3 * (size_t(x - y0) * h + (r - y0))]; }
    u64* Rp(int r) { return &rp[size_t(r - y0) * Wrp]; }
    u64* Rk(int r) { return &rk[size_t(r - y0) * Wrk]; }
    u64* Cp(int x) { return x < y1 ? Rp(x) : &cp[size_t(col_slot(x)) * Wcp]; }
    u64* Ck(int x) { return x < y1 ? Rk(x) : &ck[size_t(col_slot(x)) * Wck]; }
    u64* Dp(int d) { return &dp[size_t(diag_slot(d)) * Wdp]; }
    u64* Dk(int d) { return &dk[size_t(diag_slot(d)) * Wdk]; }
    int col_slot(int x) const { return (x - y0) % (T * B); }
    int diag_slot(int d) const { return d % (2 * T * B); }
    i64 diag_off(int d) const { return (i64(y0) + d) & ~i64(63); }

    bool cell(int i, int j)
    {
        const int d = j - i;
        const bool tri = j < y1; // column j is row j of this strip
        u64 *Pi = Rp(i), *Ki = Rk(i);
        u64 *Pj = Cp(j), *Kj = Ck(j);
        u64 *Pd = Dp(d), *Kd = Dk(d);
        const i64 oi = i64(y0) & ~i64(63), oj = tri ? oi : i64(j) & ~i64(63), od = diag_off(d);

        const std::uint32_t known = load3(at(i, j));
        i64 k = known;
        if (known == kNone) {
            i64 base = i64(j) & ~i64(63);
            const i64 limit = 2 * i64(j) + kSlack;
            // Shifted windows: word index and shift are fixed, base advances by 64.
            const i64 q1 = base - j + kBias, q2 = base - i + kBias;
            const u64 *pi = Pi + ((base - oi) >> 6), *pj = Pj + ((base - oj) >> 6), *pd = Pd + ((base - od) >> 6);
            const u64 *ki = Ki + (q1 >> 6), *kj = Kj + (q2 >> 6), *kd = Kd + (q1 >> 6);
            const unsigned s1 = q1 & 63, s2 = q2 & 63;
            u64 low = (u64{1} << (j & 63)) - 1;
            for (size_t t = 0;; t += 8, base += 512, low = 0) {
                if (base >= limit) return false;
                u64 tk[8], all = ~u64{0};
                for (size_t u = 0; u < 8; ++u) {
                    const size_t z = t + u;
                    tk[u] = pi[z] | pj[z] | pd[z] |
                            ((ki[z] | kd[z]) >> s1) | (((ki[z + 1] | kd[z + 1]) << 1) << (63 - s1)) |
                            (kj[z] >> s2) | ((kj[z + 1] << 1) << (63 - s2));
                }
                tk[0] |= low;
                for (size_t u = 0; u < 8; ++u) all &= tk[u];
                if (~all) {
                    size_t u = 0;
                    while (!~tk[u]) ++u;
                    k = base + i64(u) * 64 + __builtin_ctzll(~tk[u]);
                    break;
                }
            }
            if (k >= limit) return false;
            if (k < N) {
                store3(at(i, int(k)), j);
                if (j < y1) store3(at(j, int(k)), i);
            }
            store3(at(i, j), std::uint32_t(k));
        }
        if (k >= oi) set_bit(Pi, k - oi);
        if (k >= oj) set_bit(Pj, k - oj);
        if (k >= od) set_bit(Pd, k - od);
        if (k - j + kBias >= 0) set_bit(Ki, k - j + kBias);
        if (k - i + kBias >= 0) set_bit(Kj, k - i + kBias);
        if (k - j + kBias >= 0) set_bit(Kd, k - j + kBias);
        return true;
    }

    // Append array a[from..w) without trailing zero words.
    static void put(Buf& b, const u64* a, size_t from, size_t w)
    {
        size_t e = w - kPad + 1;
        while (e > from && a[e - 1] == 0) --e;
        const u64 n = e - from;
        const size_t p = b.size();
        b.resize(p + 8 + n * 8);
        std::memcpy(&b[p], &n, 8);
        if (n) std::memcpy(&b[p + 8], a + from, n * 8);
    }
    static void get(const Buf& b, size_t& p, u64* a)
    {
        u64 n;
        std::memcpy(&n, &b[p], 8);
        if (n) std::memcpy(a, &b[p + 8], n * 8);
        p += 8 + n * 8;
    }

    void load_fill_ins()
    {
        for (int s = 0; s < y0; s += H) {
            FILE* f = xopen(out_name(s), "rb");
            FILE* ix = xopen(out_name(s) + ".idx", "rb");
            u64 off;
            if (fseeko(ix, off_t((y0 - s) / B) * 8, SEEK_SET) || std::fread(&off, 8, 1, ix) != 1) die("index read");
            if (fseeko(f, off_t(off), SEEK_SET)) die("seek");
            std::vector<Buf> z(T);
            for (int a = 0; a < T; ++a) {
                if (!read_z(f, z[a])) die("output file too short");
                st.read_z += z[a].size();
            }
            std::fclose(f);
            std::fclose(ix);
#pragma omp parallel for schedule(dynamic, 1)
            for (int a = 0; a < T; ++a) {
                const Buf raw = decompress(z[a]);
                const size_t cells = raw.size() / 3;
                const int x0 = y0 + a * B, x1 = std::min(y1, x0 + B);
                for (int r = x0; r < x1; ++r)
                    for (int q = 0; q < H; ++q) {
                        const int k = int(shuffled(raw, cells, size_t(r - x0) * H + q));
                        if (k >= r && k < N) store3(at(r, k), s + q);
                    }
            }
        }
    }

    static constexpr size_t kDepth = 8; // state frames in flight
    Stats st;

    void strip(int y)
    {
        const double t0 = now();
        st = Stats{};
        y0 = y; y1 = std::min(N, y + H); h = y1 - y0;
        T = (h + B - 1) / B;
        M = (N - y0 + B - 1) / B;
        rp.assign(size_t(h) * Wrp, 0); rk.assign(size_t(h) * Wrk, 0);
        cp.assign(size_t(T) * B * Wcp, 0); ck.assign(size_t(T) * B * Wck, 0);
        dp.assign(size_t(2) * T * B * Wdp, 0); dk.assign(size_t(2) * T * B * Wdk, 0);
        tab.assign(3 * size_t(N - y0) * h, 0xff);

        double t = now();
        load_fill_ins();
        st.io += now() - t;

        FILE* in = y0 ? xopen(state_name(y0), "rb") : nullptr;
        const std::string stmp = state_name(y1) + ".tmp";
        FILE* sout = xopen(stmp, "wb");
        FILE* out = xopen(out_name(y0), "wb");
        Writer outw(out, 64), stw(sout, kDepth);
        std::unique_ptr<Reader> rd(in ? new Reader(in, kDepth) : nullptr);

        for (int w = 0; w <= M - 1 + T - 1; ++w) {
            t = now();
            if (w < M) enter_group(w, rd.get());
            st.io += now() - t;

            t = now();
            bool ok = true;
            // Tiles (a,b): rows y0+a*B.., columns y0+b*B.., a <= b, a+b = w.
#pragma omp parallel for schedule(dynamic, 1) reduction(&& : ok)
            for (int a = std::max(0, w - (M - 1)); a <= std::min(T - 1, w / 2); ++a) {
                const int b = w - a;
                const int i0 = y0 + a * B, i1 = std::min(y1, i0 + B);
                const int j0 = y0 + b * B, j1 = std::min(N, j0 + B);
                for (int i = i0; i < i1 && ok; ++i)
                    for (int j = std::max(i, j0); j < j1 && ok; ++j) ok = cell(i, j);
            }
            if (!ok) die("a value reached 2*max(i,j)+kSlack; raise kSlack");
            st.compute += now() - t;

            t = now();
            const int b = w - (T - 1); // column group whose last tile just ran
            if (b >= 0 && b < M) {
                const int x0 = y0 + b * B, x1 = std::min(N, x0 + B);
                // These columns are final; the worker may read them directly.
                const unsigned char* src = &tab[3 * (size_t(x0 - y0) * h)];
                const size_t cells = size_t(x1 - x0) * h;
                st.out_raw += 4 * cells;
                outw.add([src, cells] { return shuffle3(src, cells); });
                if (x0 >= y1) leave_group(x0, x1, stw);
            }
            if (w % 64 == 0) std::fprintf(stderr, "\rstrip %d: %d%%", y0, int(100LL * w / (M + T)));
            st.io += now() - t;
        }
        t = now();
        if (rd) { st.read_z += rd->bytes(); rd.reset(); }
        if (in) std::fclose(in);
        st.out_z = outw.finish();
        st.st_z = stw.finish();
        const auto& index = outw.index();
        finish(out, out_name(y0));
        FILE* ix = xopen(out_name(y0) + ".idx", "wb");
        if (std::fwrite(index.data(), 8, index.size(), ix) != index.size()) die("index write");
        finish(ix, "index");
        finish(sout, stmp);
        if (std::rename(stmp.c_str(), state_name(y1).c_str())) die("rename");
        if (y0) std::remove(state_name(y0).c_str());
        st.io += now() - t;
        std::fprintf(stderr,
                     "\rstrip %d..%d: %.2fs (compute %.2f, i/o wait %.2f) state %.1f->%.1f MB, out %.1f->%.1f MB (%.2f bits/cell), read %.1f MB (state + fill-ins)\n",
                     y0, y1, now() - t0, st.compute, st.io, st.st_raw / 1e6, st.st_z / 1e6, st.out_raw / 1e6,
                     st.out_z / 1e6, 32.0 * st.out_z / st.out_raw, st.read_z / 1e6);
    }

    // Clear the buffers of column group b and diagonal group b; load their records.
    void enter_group(int b, Reader* rd)
    {
        const int x0 = y0 + b * B, x1 = std::min(N, x0 + B);
        Buf raw;
        std::vector<size_t> pos(x1 - x0, 0);
        if (rd) {
            raw = rd->next();
            size_t p = 0;
            for (int x = x0; x < x1; ++x) {
                pos[x - x0] = p;
                for (int a = 0; a < 4; ++a) {
                    u64 n;
                    std::memcpy(&n, &raw[p], 8);
                    p += 8 + 8 * n;
                }
            }
        }
#pragma omp parallel for schedule(static)
        for (int x = x0; x < x1; ++x) {
            const int d = x - y0;
            std::fill(Dp(d), Dp(d) + Wdp, 0);
            std::fill(Dk(d), Dk(d) + Wdk, 0);
            const bool tri = x < y1;
            if (!tri) {
                std::fill(Cp(x), Cp(x) + Wcp, 0);
                std::fill(Ck(x), Ck(x) + Wck, 0);
            }
            if (rd) {
                const i64 shift = tri ? ((i64(x) & ~i64(63)) - (i64(y0) & ~i64(63))) >> 6 : 0;
                size_t p = pos[x - x0];
                get(raw, p, Cp(x) + shift);
                get(raw, p, Ck(x));
                get(raw, p, Dp(d));
                get(raw, p, Dk(d));
            }
        }
    }

    // Save records for columns x0..x1 and diagonals d = x - y1.
    void leave_group(int x0, int x1, Writer& w)
    {
        std::vector<Buf> part(x1 - x0);
#pragma omp parallel for schedule(static)
        for (int x = x0; x < x1; ++x) {
            const int d = x - y1;
            Buf& b = part[x - x0];
            put(b, Cp(x), 0, Wcp);
            put(b, Ck(x), 0, Wck);
            put(b, Dp(d), size_t(((i64(x) & ~i64(63)) - diag_off(d)) >> 6), Wdp);
            put(b, Dk(d), 0, Wdk);
        }
        for (auto& b : part) st.st_raw += b.size();
        w.add([part = std::move(part)] {
            Buf raw;
            size_t n = 0;
            for (auto& b : part) n += b.size();
            raw.reserve(n);
            for (auto& b : part) raw.insert(raw.end(), b.begin(), b.end());
            return raw;
        });
    }
};

int main(int argc, char** argv)
{
    const std::string cmd = argc > 1 ? argv[1] : "";
    if (argc < 5 || (cmd != "run" && cmd != "export") || (cmd == "export" && argc != 6)) {
        std::fprintf(stderr, "usage: %s run DIR N H [max-strips] | export DIR N H out.bin\n", argv[0]);
        return 2;
    }
    const int n = std::atoi(argv[3]), h = std::atoi(argv[4]);
    if (n <= 0 || h <= 0) { std::fprintf(stderr, "N and H must be positive\n"); return 2; }
    Strips s(argv[2], n, std::min(n, h));
    if (cmd == "run") s.run(argc > 5 ? std::atoi(argv[5]) : 1 << 30);
    else s.export_full(argv[5]);
    return 0;
}
