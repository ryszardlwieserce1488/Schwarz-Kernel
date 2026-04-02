// math.h  — stub dla kernela, żeby STB się nie poskarżył
#pragma once

// STB używa tych funkcji, ale my je nadpisujemy makrami
// Ten plik musi istnieć, żeby #include <math.h> nie wybuchło

static inline float sqrtf(float x) {
    float res;
    asm("fsqrt" : "=t"(res) : "0"(x));
    return res;
}

static inline double sqrt(double x) {
    double res;
    asm("fsqrt" : "=t"(res) : "0"(x));
    return res;
}

static inline float fabsf(float x) { return x < 0 ? -x : x; }
static inline double fabs(double x) { return x < 0 ? -x : x; }
static inline float floorf(float x) {
    int i = (int)x;
    return (float)(i - (x < 0.0f && x != (float)i));
}
static inline float ceilf(float x) {
    int i = (int)x;
    return (float)(i + (x > 0.0f && x != (float)i));
}
// --- exp ---
static inline float my_expf(float x) {
    // Redukujemy x do zakresu: e^x = 2^(x/ln2)
    // Rozbijamy na część całkowitą i ułamkową
    const float ln2 = 0.693147180f;
    const float inv_ln2 = 1.442695041f;

    float t = x * inv_ln2;
    int n = (int)t;
    if (t < 0.0f) n -= 1;
    float f = x - (float)n * ln2;  // f ∈ [0, ln2)

    // Taylor: e^f = 1 + f + f²/2! + f³/3! + ... (f jest małe, szybko zbiega)
    float result = 1.0f + f + (f * f) / 2.0f + (f * f * f) / 6.0f
        + (f * f * f * f) / 24.0f + (f * f * f * f * f) / 120.0f
        + (f * f * f * f * f * f) / 720.0f;

    // Mnożymy przez 2^n przez manipulację bitami IEEE 754
    int bits;
    __builtin_memcpy(&bits, &result, 4);
    bits += n << 23;
    __builtin_memcpy(&result, &bits, 4);
    return result;
}

// --- log (ln) ---
static inline float my_logf(float x) {
    // Wyciągamy eksponent z IEEE 754: x = m * 2^e, m ∈ [1, 2)
    int bits;
    __builtin_memcpy(&bits, &x, 4);
    int e = ((bits >> 23) & 0xFF) - 127;
    bits = (bits & 0x7FFFFF) | (127 << 23);  // m = x z eksponentem = 0
    float m;
    __builtin_memcpy(&m, &bits, 4);

    // Teraz ln(x) = e*ln(2) + ln(m), m ∈ [1,2)
    // Podstawiamy m = 1 + t, t ∈ [0, 1)
    // Ale dla lepszej zbieżności: niech m ∈ [√2/2, √2], wtedy t ∈ [-0.3, 0.3]
    if (m > 1.41421356f) { m *= 0.5f; e += 1; }

    float t = (m - 1.0f) / (m + 1.0f);  // t = (m-1)/(m+1), |t| < 0.18
    float t2 = t * t;

    // ln(m) = 2 * (t + t³/3 + t⁵/5 + t⁷/7 + ...)
    float ln_m = 2.0f * t * (1.0f + t2 / 3.0f + t2 * t2 / 5.0f
        + t2 * t2 * t2 / 7.0f + t2 * t2 * t2 * t2 / 9.0f);

    const float ln2 = 0.693147180f;
    return (float)e * ln2 + ln_m;
}
// --- cos przez szereg Taylora ---
static inline double cos(double x) {
    // Redukujemy x do [-π, π]
    const double PI = 3.14159265358979323846;
    const double TWO_PI = 6.28318530717958647692;
    while (x > PI) x -= TWO_PI;
    while (x < -PI) x += TWO_PI;

    double x2 = x * x;
    // cos(x) = 1 - x²/2! + x⁴/4! - x⁶/6! + x⁸/8! - x¹⁰/10!
    return 1.0
        - x2 / 2.0
        + x2 * x2 / 24.0
        - x2 * x2 * x2 / 720.0
        + x2 * x2 * x2 * x2 / 40320.0
        - x2 * x2 * x2 * x2 * x2 / 3628800.0;
}

// --- acos przez wzór z atan ---
// acos(x) = π/2 - asin(x)
// asin(x) = atan(x / sqrt(1 - x²))
static inline double my_atan(double x) {
    // Redukujemy do [-1, 1] przez tożsamość atan(x) = π/2 - atan(1/x) dla x>1
    const double PI = 3.14159265358979323846;
    int neg = 0, flip = 0;
    if (x < 0.0) { neg = 1; x = -x; }
    if (x > 1.0) { flip = 1; x = 1.0 / x; }

    double x2 = x * x;
    // Szereg Taylora dla małych x
    double result = x * (1.0
        - x2 / 3.0
        + x2 * x2 / 5.0
        - x2 * x2 * x2 / 7.0
        + x2 * x2 * x2 * x2 / 9.0
        - x2 * x2 * x2 * x2 * x2 / 11.0
        + x2 * x2 * x2 * x2 * x2 * x2 / 13.0);

    if (flip) result = PI / 2.0 - result;
    if (neg)  result = -result;
    return result;
}

static inline double acos(double x) {
    const double PI = 3.14159265358979323846;
    if (x > 1.0) x = 1.0;
    if (x < -1.0) x = -1.0;
    // asin(x) = atan(x / sqrt(1 - x²))
    double asin_x = my_atan(x / my_sqrt((float)(1.0 - x * x)));
    return PI / 2.0 - asin_x;
}
// --- powf ---
static inline float powf(float x, float y) {
    if (x <= 0.0f) return 0.0f;   // STB nigdy nie woła z x<=0, ale dla bezpieczeństwa
    return my_expf(y * my_logf(x));
}
static inline float fmodf(float x, float y) {
    return x - (float)(int)(x / y) * y;
}