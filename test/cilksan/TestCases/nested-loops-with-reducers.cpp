// RUN: %clangxx_cilksan -std=c++20 -fopencilk -O3 -g %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s --check-prefixes=CHECK,CILKSAN
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>

#include <cilk/cilk.h>

struct Scalar {
    constexpr static uint64_t PRIME = 0xffffffff00000001ull;

    // Scalar() { /* Deliberately skip initialization of _raw to allow more
    //               aggresive optimization */
    // }

    Scalar() = default;

    explicit Scalar(uint64_t raw) : _raw{raw} {}

    explicit operator uint64_t() const { return _raw; }

    auto operator+(Scalar other) const -> Scalar {
        const uint64_t sum = _raw + other._raw;
        const Scalar ret{
            sum < _raw || sum < other._raw || sum >= PRIME ? sum - PRIME : sum};
        return ret;
    }

    auto operator-(Scalar other) const -> Scalar {
        const uint64_t diff = _raw - other._raw;
        const Scalar ret{(diff > _raw) ? diff + PRIME : diff};
        return ret;
    }

    auto operator*(Scalar other) const -> Scalar {
        // Start by carrying out an ordinary 64x64->128 bit multiplication
        const uint32_t a0 = _raw, a1 = _raw >> 32;
        const uint32_t b0 = other._raw, b1 = other._raw >> 32;
        const uint64_t p0 = static_cast<uint64_t>(a0) * b0,
                       p1 = static_cast<uint64_t>(a0) * b1,
                       p2 = static_cast<uint64_t>(a1) * b0,
                       p3 = static_cast<uint64_t>(a1) * b1;
        const uint32_t cy = ((p0 >> 32) + static_cast<uint32_t>(p1) +
                             static_cast<uint32_t>(p2)) >>
                            32;
        const uint64_t x = p0 + (p1 << 32) + (p2 << 32),
                       y = p3 + (p1 >> 32) + (p2 >> 32) + cy;
        // Store result in 4 32-bit words
        const uint32_t c0 = x, c1 = x >> 32, c2 = y, c3 = y >> 32;
        // Now perform reduction: modulus is phi^2 - phi + 1 where phi = 2^32
        //   ab = c0 + c1*phi + c2*phi^2 + c3*phi^3
        // Exploit phi^2 = phi-1 and phi^3 = phi * (phi-1) = (phi-1) - phi = -1
        //   ab = c0 + c1*phi + c2*(phi-1) - c3
        //      = (c0-c2-c3) + (c1+c2)*phi
        const Scalar ret = (Scalar(c0) - Scalar(c2) - Scalar(c3)) +
                           (Scalar(static_cast<uint64_t>(c1) << 32) +
                            Scalar(static_cast<uint64_t>(c2) << 32));
        return ret;
    }

    auto operator==(Scalar other) const -> bool { return _raw == other._raw; }

    auto operator!=(Scalar other) const -> bool { return _raw != other._raw; }

    // No comparison operators as they make no sense for finite fields

    auto operator+=(Scalar other) -> Scalar & { return *this = *this + other; }

    auto operator-=(Scalar other) -> Scalar & { return *this = *this - other; }

    auto operator*=(Scalar other) -> Scalar & { return *this = *this * other; }

    template <typename RNG> inline static auto random(RNG &rng) -> Scalar {
        static std::uniform_int_distribution<uint64_t> dist(0, PRIME - 1);
        return Scalar{dist(rng)};
    }

    auto is_valid() const -> bool { return _raw < PRIME; }

  private:
    uint64_t _raw;
};

static inline void zero_scalar(void *view) {
    *reinterpret_cast<Scalar *>(view) = Scalar{0};
}

static inline void add_scalar(void *left, void *right) {
    *reinterpret_cast<Scalar *>(left) += *reinterpret_cast<Scalar *>(right);
}

using ScalarAddReducer = Scalar cilk_reducer(zero_scalar, add_scalar);

auto reduce_with_cilk(Scalar **as, Scalar **bs, Scalar *c, Scalar *coeffs,
                      size_t n, size_t m) -> std::array<Scalar, 3> {
    ScalarAddReducer p0{0}, p2{0}, p3{0};
    cilk_for (size_t i = 0; i < n; i++) {
        // Obtain dense representations of the polynomials
        const Scalar *a = as[i];
        const Scalar *b = bs[i];
        const size_t half = m / 2;

        ScalarAddReducer lp0{0}, lp2{0}, lp3{0};
        cilk_for (size_t j = 0; j < half; j++) {
            lp0 += a[j] * b[j] * c[j];
            const Scalar a2 = a[j + half] + a[j + half] - a[j],
                         b2 = b[j + half] + b[j + half] - b[j],
                         c2 = c[j + half] + c[j + half] - c[j];
            lp2 += a2 * b2 * c2;
            const Scalar a3 = a2 + a[j + half] - a[j],
                         b3 = b2 + b[j + half] - b[j],
                         c3 = c2 + c[j + half] - c[j];
            lp3 += a3 * b3 * c3;
        }
        p0 += coeffs[i] * lp0;
        p2 += coeffs[i] * lp2;
        p3 += coeffs[i] * lp3;
    }
    return {p0, p2, p3};
}

auto reduce_serial(Scalar **as, Scalar **bs, Scalar *c, Scalar *coeffs,
                   size_t n, size_t m) -> std::array<Scalar, 3> {
    Scalar p0{0}, p2{0}, p3{0};
    for (size_t i = 0; i < n; i++) {
        // Obtain dense representations of the polynomials
        const Scalar *a = as[i];
        const Scalar *b = bs[i];
        const size_t half = m / 2;

        Scalar lp0{0}, lp2{0}, lp3{0};
        for (size_t j = 0; j < half; j++) {
            lp0 += a[j] * b[j] * c[j];
            const Scalar a2 = a[j + half] + a[j + half] - a[j],
                         b2 = b[j + half] + b[j + half] - b[j],
                         c2 = c[j + half] + c[j + half] - c[j];
            lp2 += a2 * b2 * c2;
            const Scalar a3 = a2 + a[j + half] - a[j],
                         b3 = b2 + b[j + half] - b[j],
                         c3 = c2 + c[j + half] - c[j];
            lp3 += a3 * b3 * c3;
        }

        p0 += coeffs[i] * lp0;
        p2 += coeffs[i] * lp2;
        p3 += coeffs[i] * lp3;
    }
    return {p0, p2, p3};
}

auto main() -> int {
    const size_t N = 12;
    const size_t M = 128;

    std::mt19937_64 rng{42};

    Scalar *as[N];
    Scalar *bs[N];
    Scalar c[M];
    Scalar coeffs[N];

    for (size_t i = 0; i < N; i++) {
        as[i] = new Scalar[M];
        bs[i] = new Scalar[M];
        coeffs[i] = Scalar::random(rng);
        for (size_t j = 0; j < M; j++) {
            as[i][j] = Scalar::random(rng);
            bs[i][j] = Scalar::random(rng);
        }
    }
    for (size_t i = 0; i < M; i++) {
        c[i] = Scalar::random(rng);
    }

    const auto res_cilk = reduce_with_cilk(as, bs, c, coeffs, N, M);
    const auto res_serial = reduce_serial(as, bs, c, coeffs, N, M);
    if (res_cilk != res_serial) {
        printf("res_cilk = %lu, %lu, %lu\n", static_cast<uint64_t>(res_cilk[0]),
               static_cast<uint64_t>(res_cilk[1]),
               static_cast<uint64_t>(res_cilk[2]));
        printf("res_serial = %lu, %lu, %lu\n",
               static_cast<uint64_t>(res_serial[0]),
               static_cast<uint64_t>(res_serial[1]),
               static_cast<uint64_t>(res_serial[2]));
    }
    for (size_t i = 0; i < N; i++) {
        delete[] as[i];
        delete[] bs[i];
    }
    return 0;
}

// NOLINTEND

// CHECK-NOT: res_cilk =
// CHECK-NOT: res_serial =

// CILKSAN: Cilksan detected 0 distinct races.
// CILKSAN-NEXT: Cilksan suppressed 0 duplicate race reports.
