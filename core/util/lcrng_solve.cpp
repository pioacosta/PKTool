#include "core/util/lcrng_solve.h"

namespace pktool::util {
namespace {

constexpr int64_t kModulus = int64_t(1) << 32;
constexpr int64_t kLow = 65536;  // the unknown lower half of the state: x in [0, kLow)

int64_t floorDiv(int64_t n, int64_t d) {  // d > 0
    int64_t q = n / d;
    return (n % d != 0 && n < 0) ? q - 1 : q;
}

int64_t ceilDiv(int64_t n, int64_t d) {  // d > 0
    return -floorDiv(-n, d);
}

int64_t minOf(int64_t a, int64_t b, int64_t c, int64_t d) {
    int64_t m = a < b ? a : b;
    if (c < m) m = c;
    return d < m ? d : m;
}

int64_t maxOf(int64_t a, int64_t b, int64_t c, int64_t d) {
    int64_t m = a > b ? a : b;
    if (c > m) m = c;
    return d > m ? d : m;
}

// Every x in [0, 65536) with ((mul * x - t) mod 2^32) < window, ascending, appended to `out`
// after the `count` values already there; returns the new count (capped at `max`).
//
// The points (x, y) with y = mul * x (mod 2^32) form a lattice of determinant 2^32. A basis comes
// from the Euclidean algorithm on (2^32, mul): two consecutive remainders r with their
// coefficients t, so that r = t * mul (mod 2^32), taken where r drops below 2^16. The wanted
// points are those of the shifted lattice p0 + i * b1 + j * b2, with p0 = (0, -t), lying in the
// box [0, 65536) x [0, window): the box is about as large as the lattice's cell, so only a few
// integer pairs (i, j) can reach it.
int solveWindow(uint32_t mul, uint32_t t, int64_t window, uint16_t* out, int count, int max) {
    int64_t tPrev = 0, rPrev = kModulus;
    int64_t tCur = 1, rCur = mul;
    while (rCur >= kLow) {
        const int64_t q = rPrev / rCur;
        const int64_t tNext = tPrev - q * tCur;
        const int64_t rNext = rPrev - q * rCur;
        tPrev = tCur;
        rPrev = rCur;
        tCur = tNext;
        rCur = rNext;
    }
    // Basis b1 = (tCur, rCur), b2 = (tPrev, rPrev); det = +-2^32.
    const int64_t det = tCur * rPrev - rCur * tPrev;
    const int64_t sign = det > 0 ? 1 : -1;

    // Coordinates (i, j) of the four corners of the box, relative to p0 = (0, -t).
    const int64_t dxs[2] = {0, kLow - 1};
    const int64_t dys[2] = {static_cast<int64_t>(t), static_cast<int64_t>(t) + window - 1};
    int64_t ni[4], nj[4];
    for (int c = 0; c < 4; ++c) {
        const int64_t dx = dxs[c & 1];
        const int64_t dy = dys[c >> 1];
        ni[c] = (dx * rPrev - dy * tPrev) * sign;  // i = ni / 2^32
        nj[c] = (tCur * dy - rCur * dx) * sign;
    }
    const int64_t iLow = ceilDiv(minOf(ni[0], ni[1], ni[2], ni[3]), kModulus);
    const int64_t iHigh = floorDiv(maxOf(ni[0], ni[1], ni[2], ni[3]), kModulus);
    const int64_t jLow = ceilDiv(minOf(nj[0], nj[1], nj[2], nj[3]), kModulus);
    const int64_t jHigh = floorDiv(maxOf(nj[0], nj[1], nj[2], nj[3]), kModulus);

    const int first = count;
    for (int64_t i = iLow; i <= iHigh; ++i) {
        for (int64_t j = jLow; j <= jHigh; ++j) {
            const int64_t x = i * tCur + j * tPrev;
            if (x < 0 || x >= kLow) continue;
            const int64_t y = -static_cast<int64_t>(t) + i * rCur + j * rPrev;
            if (y < 0 || y >= window) continue;
            if (count < max) out[count] = static_cast<uint16_t>(x);
            if (count < max) ++count;
        }
    }
    // Insertion sort of the few values found.
    for (int a = first + 1; a < count; ++a) {
        const uint16_t v = out[a];
        int b = a - 1;
        for (; b >= first && out[b] > v; --b) out[b + 1] = out[b];
        out[b + 1] = v;
    }
    return count;
}

// Values x with (mul * (upper16 << 16 | x) + add) mod 2^32 in [lo, lo + 65536) (cyclically).
int solveRange(Lcg lcg, uint32_t upper16, uint32_t lo, uint16_t* out, int count, int max) {
    const uint32_t atZero = lcg.mul * (upper16 << 16) + lcg.add;  // output for x = 0
    return solveWindow(lcg.mul, lo - atZero, kLow, out, count, max);
}

}  // namespace

Lcg lcgJump(Lcg lcg, int steps) {
    Lcg result = {1, 0};
    for (int i = 0; i < steps; ++i) {
        result.add = result.add * lcg.mul + lcg.add;
        result.mul = result.mul * lcg.mul;
    }
    return result;
}

int lcgSolveTop16(Lcg lcg, uint32_t upper16, uint32_t wanted16, uint16_t* out, int max) {
    return solveRange(lcg, upper16 & 0xFFFF, (wanted16 & 0xFFFF) << 16, out, 0, max);
}

int lcgSolveWord15(Lcg lcg, uint32_t upper16, uint32_t word15, uint16_t* out, int max) {
    // Bit 31 of the output is free: two windows, merged into ascending order.
    uint16_t low[16];
    uint16_t high[16];
    const int a = solveRange(lcg, upper16 & 0xFFFF, (word15 & 0x7FFF) << 16, low, 0, 16);
    const int b = solveRange(lcg, upper16 & 0xFFFF, ((word15 & 0x7FFF) | 0x8000u) << 16, high, 0, 16);
    int i = 0, j = 0, n = 0;
    while ((i < a || j < b) && n < max) {
        out[n++] = (j >= b || (i < a && low[i] < high[j])) ? low[i++] : high[j++];
    }
    return n;
}

}  // namespace pktool::util
