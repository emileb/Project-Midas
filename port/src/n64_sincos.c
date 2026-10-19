/**
 * n64_sincos.c - PC-side C body for the game's hand-written sinf/cosf
 * (src/game/math_sincos.s), which every sinf/cosf call on N64 resolves to,
 * game code and libultra gu alike (libultra's own are named fsin/fcos).
 *
 * The port used to link libm's sinf/cosf instead. Those are more accurate
 * but differ in the last bits, so anything built on them (movement, aim,
 * animation blends) drifted from the N64 and the attract demos lost sync.
 * This is an instruction-for-instruction translation: the same single-
 * precision operations in the same order, which IEEE 754 makes bit-exact
 * on any FPU without fused multiply-add. CMake maps sinf/cosf in C sources
 * onto these names, so the compiler can't fold or merge them as builtins.
 */
#include <ultra64.h>

#pragma STDC FP_CONTRACT OFF
#ifdef __clang__
#pragma clang fp contract(off)
#endif

#undef sinf
#undef cosf

typedef union {
    u32 u;
    f32 f;
} N64F32;

static const N64F32 kPiOver2 = { 0x3fc90fda };     /* D_800536C0 1.5707963 */
static const N64F32 kSmallC4 = { 0x362edef8 };     /* D_800536C4 / E0: x^7 term */
static const N64F32 kSmallC8 = { 0xb94fb7ff };     /* D_800536C8 / E4: x^5 term */
static const N64F32 kSmallCC = { 0x3c08876a };     /* D_800536CC / E8: x^3 term */
static const N64F32 kSmallD0 = { 0xbe2aaaa6 };     /* D_800536D0 / EC: x^1 term */
static const N64F32 kInvPi = { 0x3ea2f983 };       /* D_800536D4 0.31830987 */
static const N64F32 kPiHi = { 0x40490fdb };        /* D_800536D8 3.1415927 */
static const N64F32 kPiLo = { 0x330885a3 };        /* D_800536DC 3.1786509e-08 */

/* odd polynomial in x, evaluated in math_sincos.s's order */
static f32 n64SinPoly(f32 x)
{
    f32 x2 = x * x;
    f32 r = kSmallC4.f * x2;

    r = kSmallC8.f + r;
    r = r * x2;
    r = kSmallCC.f + r;
    r = r * x2;
    r = kSmallD0.f + r;
    r = r * x2;
    r = r * x;
    return r + x;
}

f32 portN64Sinf(f32 x)
{
    N64F32 v;
    u32 e;
    f32 n;
    s32 q;

    v.f = x;
    e = (u32) (((s32) v.u >> 22) & 0x1ff); /* exponent + top mantissa bit */

    if (e < 0xff) /* |x| < 1.5 */
    {
        if (e < 0xe6)
        {
            return x; /* tiny: sin x = x */
        }
        return n64SinPoly(x);
    }

    if (e >= 0x136)
    {
        return 0.0f; /* too large to reduce */
    }

    /* x -= n * pi (pi split in two), n = round-to-nearest(x / pi) */
    n = kInvPi.f * x;
    q = (s32) __builtin_rintf(n); /* round.w.s: default round-to-nearest-even */
    n = (f32) q;
    x = x - kPiHi.f * n;
    x = x - kPiLo.f * n;

    x = n64SinPoly(x);
    return (q & 1) ? -x : x;
}

f32 portN64Cosf(f32 x)
{
    return portN64Sinf(x + kPiOver2.f);
}
