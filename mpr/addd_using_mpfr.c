#include "mpr.h"

/*
Computes z = x + y / 2^shift, rounding down (truncating) with correct rounding.
Returns adjustment (0 or 1) to be added to the exponent.
*/

int
_mpr_addd_using_mpfr(mp_ptr z, mp_size_t zn, mp_srcptr x, mp_size_t xn, mp_srcptr y, mp_size_t yn, long shift)
{
    __mpfr_struct zs, xs, ys;

    xs._mpfr_d = (mp_ptr) x;
    xs._mpfr_exp = 0;
    xs._mpfr_prec = xn * FLINT_BITS;
    xs._mpfr_sign = 1;

    ys._mpfr_d = (mp_ptr) y;
    ys._mpfr_exp = -shift;
    ys._mpfr_prec = yn * FLINT_BITS;
    ys._mpfr_sign = 1;

    zs._mpfr_d = (mp_ptr) z;
    zs._mpfr_exp = 0;
    zs._mpfr_prec = zn * FLINT_BITS;
    zs._mpfr_sign = 1;

    mpfr_add(&zs, &xs, &ys, MPFR_RNDD);

    return zs._mpfr_exp != 0;
}
