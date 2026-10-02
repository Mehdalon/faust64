/*======================================================================
 * faustfx.h - fixed-point support for Faust's -fx output on CPUs.
 *
 * WHY THIS EXISTS
 *
 * Faust can already emit fixed-point code (-fx), but its shipped support
 * (faust/dsp/fixed-point.h) is built on Xilinx's ap_fixed, which is an FPGA
 * synthesis library: with -fx alone Faust gives every variable its own width
 * from range analysis, and those run to 239 bits. You can synthesise a
 * 239-bit adder in fabric; you cannot run one on a 68000.
 *
 * The key is -fx-size 32, which collapses every type to at most 32 bits and
 * leaves only the BINARY POINT differing per variable. That is an ordinary
 * int32_t with a compile-time scale, which any CPU can do. This header
 * implements exactly that, with no Xilinx dependency:
 *
 *     faust -lang c -fx -fx-size 32 -a <arch> -o out.cpp foo.dsp
 *
 * and compile the result as C++ (the generated code uses infix operators and
 * casts, so the type has to be a class).
 *
 * REPRESENTATION
 *
 *   fxp<M,L> holds an int32_t `raw`, and the value it means is raw * 2^L.
 *   Faust's macro sfx_t(m,l) maps straight onto it; the width it implies is
 *   W = (m+1) - l + 1, which -fx-size 32 keeps within 32 bits.
 *
 * ARITHMETIC
 *
 *   Every operation in the generated code is immediately cast to a named
 *   target type, so operators return an intermediate fxi<L> carrying an
 *   int64_t and a compile-time exponent. Nothing is rounded until it lands in
 *   a real fxp, which keeps a multiply exact on the way through.
 *
 * TRANSCENDENTALS
 *
 *   powfx/tanfx/... go through double. That is deliberate and it is the one
 *   place float survives. Measured on demos/acid.dsp: the per-sample loop
 *   calls pow twice and tan once ONLY because its filter cutoff is modulated
 *   per sample; a DSP with a static cutoff has none in the loop at all, and
 *   the control-rate section runs once per block. The dozens of adds and
 *   multiplies in between - the actual bulk - are integer either way.
 *
 * SATURATION matches AP_SAT: results clamp to the target width rather than
 * wrapping. On a machine that traps FP overflow this is the difference
 * between a loud sound and a crash.
 *====================================================================*/
#ifndef FAUSTFX_H
#define FAUSTFX_H

#include <stdint.h>
#include <math.h>

/* ---- compile-time helpers ------------------------------------------- */
template <int A, int B> struct fx_min_ { enum { value = (A < B) ? A : B }; };
template <int A, int B> struct fx_max_ { enum { value = (A > B) ? A : B }; };

/* Shift a 64-bit value from exponent FROM to exponent TO. Rounding is
 * to-nearest on the way down, which is what AP_RND_CONV does closely enough
 * for audio; truncation alone puts a DC offset on every signal. */
static inline int64_t fx_rescale(int64_t v, int from, int to)
{
    if (from == to) return v;
    if (from > to) {
        int sh = from - to;
        if (sh >= 63) return 0;
        return v << sh;
    }
    int sh = to - from;
    if (sh >= 63) return (v < 0) ? -1 : 0;
    int64_t add = (int64_t)1 << (sh - 1);
    return (v + add) >> sh;
}

/* ---- the intermediate: an int64 with a compile-time exponent --------- */
template <int L> struct fxi {
    int64_t v;
    explicit fxi(int64_t x) : v(x) {}
};

/* ---- the value type: raw * 2^L, W = (M+1)-L+1 bits ------------------- */
template <int M, int L> struct fxp {
    int32_t raw;

    enum { WIDTH = (M + 1) - L + 1 };

    static inline int32_t sat(int64_t x)
    {
        /* clamp to this type's width, like AP_SAT */
        const int w = (WIDTH > 32) ? 32 : ((WIDTH < 2) ? 2 : WIDTH);
        const int64_t hi = ((int64_t)1 << (w - 1)) - 1;
        const int64_t lo = -((int64_t)1 << (w - 1));
        if (x > hi) return (int32_t)hi;
        if (x < lo) return (int32_t)lo;
        return (int32_t)x;
    }

    fxp() : raw(0) {}
    fxp(int x)            { raw = sat(fx_rescale((int64_t)x, 0, L)); }
    fxp(long x)           { raw = sat(fx_rescale((int64_t)x, 0, L)); }
    fxp(double d)         { raw = sat((int64_t)llround(ldexp(d, -L))); }
    fxp(float f)          { raw = sat((int64_t)llround(ldexp((double)f, -L))); }

    template <int L2> fxp(const fxi<L2> &x) { raw = sat(fx_rescale(x.v, L2, L)); }
    template <int M2, int L2> fxp(const fxp<M2, L2> &x)
    { raw = sat(fx_rescale((int64_t)x.raw, L2, L)); }

    operator double() const { return ldexp((double)raw, L); }
    operator float()  const { return (float)ldexp((double)raw, L); }
    /* C casts truncate TOWARD ZERO; fx_rescale rounds to nearest, which is
     * right for signals and wrong for an (int) cast - Faust uses those for
     * table indices and counters, where being one out matters. */
    operator int() const
    {
        if (L >= 0) return (int)((int64_t)raw << L);
        int sh = -L;
        if (sh >= 63) return 0;
        int64_t v = raw;
        return (int)((v < 0) ? -((-v) >> sh) : (v >> sh));
    }

    double toDouble() const { return ldexp((double)raw, L); }
};

/* ---- arithmetic ------------------------------------------------------ */
#define FX_ALIGN(L1, L2) (fx_min_<(L1), (L2)>::value)

template <int M1, int L1, int M2, int L2>
static inline fxi<FX_ALIGN(L1, L2)> operator+(const fxp<M1, L1> &a, const fxp<M2, L2> &b)
{
    const int L = FX_ALIGN(L1, L2);
    return fxi<L>(fx_rescale((int64_t)a.raw, L1, L) + fx_rescale((int64_t)b.raw, L2, L));
}

template <int M1, int L1, int M2, int L2>
static inline fxi<FX_ALIGN(L1, L2)> operator-(const fxp<M1, L1> &a, const fxp<M2, L2> &b)
{
    const int L = FX_ALIGN(L1, L2);
    return fxi<L>(fx_rescale((int64_t)a.raw, L1, L) - fx_rescale((int64_t)b.raw, L2, L));
}

/* exact: the product of two 32-bit values fits in 64, so nothing is lost
 * until it is stored into a named type */
template <int M1, int L1, int M2, int L2>
static inline fxi<(L1 + L2)> operator*(const fxp<M1, L1> &a, const fxp<M2, L2> &b)
{
    return fxi<(L1 + L2)>((int64_t)a.raw * (int64_t)b.raw);
}

/* the numerator is pre-shifted so the quotient keeps fractional bits */
template <int M1, int L1, int M2, int L2>
static inline fxi<(L1 - L2 - 24)> operator/(const fxp<M1, L1> &a, const fxp<M2, L2> &b)
{
    int64_t d = (int64_t)b.raw;
    if (d == 0) d = (a.raw < 0) ? -1 : 1;          /* no traps, no NaNs */
    return fxi<(L1 - L2 - 24)>(((int64_t)a.raw << 24) / d);
}

template <int M, int L>
static inline fxp<M, L> operator-(const fxp<M, L> &a)
{ fxp<M, L> r; r.raw = -a.raw; return r; }

#define FX_CMP(OP)                                                            \
template <int M1, int L1, int M2, int L2>                                     \
static inline bool operator OP(const fxp<M1, L1> &a, const fxp<M2, L2> &b)    \
{                                                                             \
    const int L = FX_ALIGN(L1, L2);                                           \
    return fx_rescale((int64_t)a.raw, L1, L) OP fx_rescale((int64_t)b.raw, L2, L); \
}
FX_CMP(<) FX_CMP(>) FX_CMP(<=) FX_CMP(>=) FX_CMP(==) FX_CMP(!=)
#undef FX_CMP

/* ---- Faust's macros -------------------------------------------------- */
#define sfx_t(m, l) fxp<(m), (l)>
#define ufx_t(m, l) fxp<(m), (l)>
/* Faust's own default is ap_fixed<32,8>: 32 bits total, 8 integer bits
 * (sign included), 24 fractional. In this encoding that is M=6, L=-24,
 * which gives W = (6+1)-(-24)+1 = 32. */
typedef fxp<6, -24> fixpoint_t;

/* ---- the functions Faust calls --------------------------------------- */
/* integer-exact, no float anywhere */
template <int M1, int L1, int M2, int L2>
static inline fxi<FX_ALIGN(L1, L2)> fmaxfx(const fxp<M1, L1> &a, const fxp<M2, L2> &b)
{
    const int L = FX_ALIGN(L1, L2);
    int64_t x = fx_rescale((int64_t)a.raw, L1, L), y = fx_rescale((int64_t)b.raw, L2, L);
    return fxi<L>(x > y ? x : y);
}
template <int M1, int L1, int M2, int L2>
static inline fxi<FX_ALIGN(L1, L2)> fminfx(const fxp<M1, L1> &a, const fxp<M2, L2> &b)
{
    const int L = FX_ALIGN(L1, L2);
    int64_t x = fx_rescale((int64_t)a.raw, L1, L), y = fx_rescale((int64_t)b.raw, L2, L);
    return fxi<L>(x < y ? x : y);
}
template <int M, int L>
static inline fxi<L> fabsfx(const fxp<M, L> &a)
{ int64_t v = a.raw; return fxi<L>(v < 0 ? -v : v); }

/* these go through double - see the header comment */
#define FX_FN1(NAME, CALL)                                                    \
template <int M, int L>                                                       \
static inline fixpoint_t NAME(const fxp<M, L> &a)                             \
{ return fixpoint_t(CALL(a.toDouble())); }
#define FX_FN2(NAME, CALL)                                                    \
template <int M1, int L1, int M2, int L2>                                     \
static inline fixpoint_t NAME(const fxp<M1, L1> &a, const fxp<M2, L2> &b)     \
{ return fixpoint_t(CALL(a.toDouble(), b.toDouble())); }

FX_FN1(sinfx, sin)     FX_FN1(cosfx, cos)      FX_FN1(tanfx, tan)
FX_FN1(asinfx, asin)   FX_FN1(acosfx, acos)    FX_FN1(atanfx, atan)
FX_FN1(sinhfx, sinh)   FX_FN1(coshfx, cosh)    FX_FN1(tanhfx, tanh)
FX_FN1(expfx, exp)     FX_FN1(logfx, log)      FX_FN1(log10fx, log10)
FX_FN1(sqrtfx, sqrt)   FX_FN1(floorfx, floor)  FX_FN1(ceilfx, ceil)
FX_FN1(roundfx, round) FX_FN1(exp10fx, exp10)
FX_FN2(powfx, pow)     FX_FN2(fmodfx, fmod)    FX_FN2(atan2fx, atan2)
FX_FN2(remainderfx, remainder)
#undef FX_FN1
#undef FX_FN2

#endif /* FAUSTFX_H */
