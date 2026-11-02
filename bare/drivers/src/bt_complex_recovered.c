/* Recovered from libbtctrl.a:complex.o, not vendor source.
 * Preserve RV32 overflow, rounding and DIV edge cases, including a nonzero
 * divisor pair whose squared magnitude wraps to zero. Do not widen to 64 bits.
 */
#include "bt_complex_recovered.h"

rf_pair complex_set_into_brave(rf_pair a, rf_pair b)
{
    return (rf_pair){a.x + b.x, a.y + b.y};
}

rf_pair bb_rf_fetch_without_dish(rf_pair a, rf_pair b)
{
    return (rf_pair){a.x - b.x, a.y - b.y};
}

rf_pair bb_rf_make_in_job(rf_pair a, rf_pair b)
{
    return (rf_pair){a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x};
}

/* Express signed RV32 DIV using unsigned magnitudes. This also preserves
 * INT_MIN / -1 without C signed overflow or undefined division by zero.
 */
static uint32_t divide_signed_bits(uint32_t numerator, uint32_t denominator)
{
    if (!denominator)
        return UINT32_MAX;
    uint32_t n = numerator >> 31 ? 0u - numerator : numerator;
    uint32_t d = denominator >> 31 ? 0u - denominator : denominator;
    uint32_t quotient = n / d;
    return (numerator ^ denominator) >> 31 ? 0u - quotient : quotient;
}

rf_pair bb_rf_make_for_branch(rf_pair a, rf_pair b)
{
    if (!(b.x | b.y))
        return (rf_pair){0, 0};

    uint32_t denominator = b.x * b.x + b.y * b.y;
    uint32_t real = a.x * b.x + a.y * b.y;
    uint32_t imag = a.y * b.x - a.x * b.y;
    /* abs(arithmetic_shift_right(denominator, 1)), exactly as the object.
     * For negative odd values this differs from abs(denominator) / 2.
     */
    uint32_t shifted = (denominator >> 1) | (denominator & UINT32_C(0x80000000));
    uint32_t sign = 0u - (denominator >> 31);
    uint32_t half = (shifted ^ sign) - sign;
    real = real >> 31 ? real - half : real + half;
    imag = imag >> 31 ? imag - half : imag + half;
    return (rf_pair){divide_signed_bits(real, denominator),
                     divide_signed_bits(imag, denominator)};
}
