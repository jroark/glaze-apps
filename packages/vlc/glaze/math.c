/*
 * The libm functions VLC's kept code calls (glaze/sysinc/math.h). They run
 * in soft float and only at set-up time (i420_rgb's gamma table), so they
 * favour small and simple over fast: range reduction plus short series,
 * good to about 1e-15 relative.
 */
#include <math.h>
#include <stdint.h>

#define LN2     0.69314718055994530942

double fabs(double x)
{
    return x < 0.0 ? -x : x;
}

double floor(double x)
{
    if (x != x || x >= 4503599627370496.0 || x <= -4503599627370496.0)
        return x;               /* NaN, or no fraction bits left */
    double t = (double)(long long)x;
    return t > x ? t - 1.0 : t;
}

double ceil(double x)
{
    return -floor(-x);
}

double exp(double x)
{
    if (x != x)
        return x;
    if (x > 709.78)
        return HUGE_VAL;
    if (x < -745.13)
        return 0.0;
    /* x = k ln2 + r, |r| <= ln2/2; e^r by its Taylor series. */
    int k = (int)floor(x / LN2 + 0.5);
    double r = x - k * LN2, term = 1.0, sum = 1.0;
    for (int n = 1; n < 20; n++) {
        term *= r / n;
        sum += term;
    }
    /* Scale by 2^k through the exponent field, in two steps for range. */
    union { double d; uint64_t u; } s;
    int k1 = k / 2, k2 = k - k1;
    s.u = (uint64_t)(1023 + k1) << 52;
    sum *= s.d;
    s.u = (uint64_t)(1023 + k2) << 52;
    return sum * s.d;
}

double log(double x)
{
    if (x != x || x < 0.0)
        return __builtin_nan("");
    if (x == 0.0)
        return -HUGE_VAL;
    if (x > 1.7976931348623157e308)
        return x;
    /* x = m 2^e with m in [sqrt(1/2), sqrt(2)); ln m = 2 atanh((m-1)/(m+1)). */
    union { double d; uint64_t u; } v = { x };
    int e = (int)((v.u >> 52) & 0x7ff);
    if (e == 0) {               /* subnormal: scale up first */
        v.d *= 18014398509481984.0;     /* 2^54 */
        e = (int)((v.u >> 52) & 0x7ff) - 54;
    }
    e -= 1022;
    v.u = (v.u & 0x000fffffffffffffull) | 0x3fe0000000000000ull;    /* [0.5, 1) */
    double m = v.d;
    if (m < 0.70710678118654752440) {
        m *= 2.0;
        e--;
    }
    double t = (m - 1.0) / (m + 1.0), t2 = t * t, term = t, sum = 0.0;
    for (int n = 1; n < 40; n += 2) {
        sum += term / n;
        term *= t2;
    }
    return 2.0 * sum + e * LN2;
}

double log10(double x)
{
    return log(x) / 2.30258509299404568402;
}

double sqrt(double x)
{
    if (x != x || x < 0.0)
        return __builtin_nan("");
    if (x == 0.0 || x > 1.7976931348623157e308)
        return x;
    double g = exp(0.5 * log(x));
    for (int i = 0; i < 3; i++)         /* Newton polishes the last bits */
        g = 0.5 * (g + x / g);
    return g;
}

double pow(double x, double y)
{
    if (y == 0.0)
        return 1.0;
    if (x != x || y != y)
        return __builtin_nan("");
    if (x == 0.0)
        return y > 0.0 ? 0.0 : HUGE_VAL;
    if (x < 0.0) {
        /* Only integer powers of a negative base are real. */
        if (floor(y) != y)
            return __builtin_nan("");
        double r = exp(y * log(-x));
        return fabs(y) < 9007199254740992.0 && ((long long)y & 1) ? -r : r;
    }
    return exp(y * log(x));
}

float floorf(float x) { return (float)floor(x); }
float sqrtf(float x) { return (float)sqrt(x); }
float powf(float x, float y) { return (float)pow(x, y); }
float log10f(float x) { return (float)log10(x); }
