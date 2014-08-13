/*=============================================================================

    This file is part of ARB.

    ARB is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    ARB is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with ARB; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

=============================================================================*/
/******************************************************************************

    Copyright (C) 2012-2014 Fredrik Johansson

******************************************************************************/

#include "arb.h"

#define TMP_ALLOC_LIMBS(size) TMP_ALLOC((size) * sizeof(mp_limb_t))

/* atan(x) = x + eps, |eps| < x^3*/
static void
arb_atan_eps(arb_t z, const arf_t x, long prec)
{
    fmpz_t mag;
    fmpz_init(mag);
    fmpz_mul_ui(mag, ARF_EXPREF(x), 3);
    arb_set_arf(z, x);
    arb_set_round(z, z, prec);
    arb_add_error_2exp_fmpz(z, mag);
    fmpz_clear(mag);
}

/* atan(x) = pi/2 - eps, eps < 1/x <= 2^(1-mag) */
static void
arb_atan_inf_eps(arb_t z, const arf_t x, long prec)
{
    fmpz_t mag;
    fmpz_init(mag);

    fmpz_neg(mag, ARF_EXPREF(x));
    fmpz_add_ui(mag, mag, 1);

    if (arf_sgn(x) > 0)
    {
        arb_const_pi(z, prec);
    }
    else
    {
        arb_const_pi(z, prec);
        arb_neg(z, z);
    }

    arb_mul_2exp_si(z, z, -1);
    arb_add_error_2exp_fmpz(z, mag);

    fmpz_clear(mag);
}

static void
arf_atan_via_mpfr(arf_t z, const arf_t x, long prec, arf_rnd_t rnd)
{
    mpfr_t xf, zf;
    mp_ptr zptr, tmp;
    mp_srcptr xptr;
    mp_size_t xn, zn, val;
    TMP_INIT;
    TMP_START;

    zn = (prec + FLINT_BITS - 1) / FLINT_BITS;
    tmp = TMP_ALLOC(zn * sizeof(mp_limb_t));

    ARF_GET_MPN_READONLY(xptr, xn, x);

    xf->_mpfr_d = (mp_ptr) xptr;
    xf->_mpfr_prec = xn * FLINT_BITS;
    xf->_mpfr_sign = ARF_SGNBIT(x) ? -1 : 1;
    xf->_mpfr_exp = ARF_EXP(x);

    zf->_mpfr_d = tmp;
    zf->_mpfr_prec = prec;
    zf->_mpfr_sign = 1;
    zf->_mpfr_exp = 0;

    mpfr_set_emin(MPFR_EMIN_MIN);
    mpfr_set_emax(MPFR_EMAX_MAX);

    mpfr_atan(zf, xf, arf_rnd_to_mpfr(rnd));

    val = 0;
    while (tmp[val] == 0)
        val++;

    ARF_GET_MPN_WRITE(zptr, zn - val, z);
    flint_mpn_copyi(zptr, tmp + val, zn - val);
    if (zf->_mpfr_sign < 0)
        ARF_NEG(z);

    fmpz_set_si(ARF_EXPREF(z), zf->_mpfr_exp);

    TMP_END;
}

int _arf_get_integer_mpn(mp_ptr y, mp_srcptr x, mp_size_t xn, long exp);

static int
_arf_set_mpn_fixed(arf_t z, mp_srcptr xp, mp_size_t xn, mp_size_t fixn, int negative, long prec)
{
    long exp, exp_shift;
    int inexact;

    exp = (long)(xn - fixn) * FLINT_BITS;

    while (xn > 0 && xp[xn-1] == 0)
    {
        xn--;
        exp -= FLINT_BITS;
    }

    if (xn == 0)
    {
        arf_zero(z);
        return 0;
    }
    else
    {
        inexact = _arf_set_round_mpn(z, &exp_shift, xp, xn, negative, prec, ARB_RND);

        fmpz_set_si(ARF_EXPREF(z), exp + exp_shift);
        return inexact;
    }
}

void
mag_add_ui_2exp_si(mag_t z, const mag_t x, ulong y, long e)
{
    mag_t t;
    mag_init(t);
    mag_set_ui_2exp_si(t, y, e);
    mag_add(z, x, t);
    mag_clear(t);
}

void
arb_atan_arf(arb_t z, const arf_t x, long prec)
{
    if (arf_is_special(x))
    {
        if (arf_is_zero(x))
        {
            arb_zero(z);
        }
        else if (arf_is_pos_inf(x))
        {
            arb_const_pi(z, prec);
            arb_mul_2exp_si(z, z, -1);
        }
        else if (arf_is_neg_inf(x))
        {
            arb_const_pi(z, prec);
            arb_mul_2exp_si(z, z, -1);
            arb_neg(z, z);
        }
        else
        {
            arb_indeterminate(z);
        }
    }
    else if (COEFF_IS_MPZ(*ARF_EXPREF(x)))
    {
        if (fmpz_sgn(ARF_EXPREF(x)) < 0)
            arb_atan_eps(z, x, prec);
        else
            arb_atan_inf_eps(z, x, prec);
    }
    else
    {
        long exp, wp, wn, N, r;
        mp_srcptr xp;
        mp_size_t xn, tn;
        mp_ptr tmp, w, t, u;
        mp_limb_t p1, q1bits, p2, q2bits, error, error2;
        int negative, inexact, reciprocal;
        TMP_INIT;

        exp = ARF_EXP(x);
        negative = ARF_SGNBIT(x);

        if (exp < -(prec/3) - 2 || exp > prec + 2)
        {
            if (exp < 0)
                arb_atan_eps(z, x, prec);
            else
                arb_atan_inf_eps(z, x, prec);
            return;
        }

        ARF_GET_MPN_READONLY(xp, xn, x);

        /* Special case: +/- 1 (we require |x| != 1 later on) */
        if (exp == 1 && xn == 1 && xp[xn-1] == LIMB_TOP)
        {
            arb_const_pi(z, prec);
            arb_mul_2exp_si(z, z, -2);
            if (negative)
                arb_neg(z, z);
            return;
        }

        /* Absolute working precision (NOT rounded to a limb multiple) */
        wp = prec - FLINT_MIN(0, exp) + 4;

        /* Too high precision to use table */
        if (wp > ARB_ATAN_TAB2_PREC)
        {
            arf_atan_via_mpfr(arb_midref(z), x, prec, ARB_RND);
            arf_mag_set_ulp(arb_radref(z), arb_midref(z), prec);
            return;
        }

        /* Working precision in limbs */
        wn = (wp + FLINT_BITS - 1) / FLINT_BITS;

        TMP_START;

        tmp = TMP_ALLOC_LIMBS(4 * wn + 3);
        w = tmp;        /* requires wn+1 limbs */
        t = w + wn + 1; /* requires wn+1 limbs */
        u = t + wn + 1; /* requires 2wn+1 limbs */

        /* ----------------------------------------------------------------- */
        /* Convert x or 1/x to a fixed-point number |w| < 1                  */
        /* ----------------------------------------------------------------- */

        if (exp <= 0)  /* |x| < 1 */
        {
            reciprocal = 0;

            /* todo: just zero top */
            flint_mpn_zero(w, wn);

            /* w = x as a fixed-point number */
            error = _arf_get_integer_mpn(w, xp, xn, exp + wn * FLINT_BITS);
        }
        else    /* |x| > 1 */
        {
            long one_exp, one_limbs, one_bits;
            mp_ptr one;

            reciprocal = 1;

            one_exp = xn * FLINT_BITS + wn * FLINT_BITS - exp;

            flint_mpn_zero(w, wn);

            /* 1/x becomes zero */
            if (one_exp >= FLINT_BITS - 1)
            {
                /* w = 1/x */
                one_limbs = one_exp / FLINT_BITS;
                one_bits = one_exp % FLINT_BITS;

                if (one_limbs + 1 >= xn)
                {
                    one = TMP_ALLOC_LIMBS(one_limbs + 1);
                    flint_mpn_zero(one, one_limbs);
                    one[one_limbs] = UWORD(1) << one_bits;

                    /* todo: only zero necessary part */
                    flint_mpn_zero(w, wn);
                    mpn_tdiv_q(w, one, one_limbs + 1, xp, xn);

                    /* Now w must be < 1 since x > 1 and we rounded down; thus
                       w[wn] must be zero */
                }
            }

            /* todo: moderate powers of two would be exact... */
            error = 1;
        }

        /* ----------------------------------------------------------------- */
        /* Table-based argument reduction                                    */
        /* ----------------------------------------------------------------- */

        /* choose p such that p/q <= x < (p+1)/q */
        if (wp <= ARB_ATAN_TAB1_PREC)
            q1bits = ARB_ATAN_TAB1_BITS;
        else
            q1bits = ARB_ATAN_TAB21_BITS;

        p1 = w[wn-1] >> (FLINT_BITS - q1bits);

        /* atan(w) = atan(p/q) + atan(w2) */
        /* where w2 = (q*w-p)/(q+p*w) */
        if (p1 != 0)
        {
            t[wn] = (UWORD(1) << q1bits) + mpn_mul_1(t, w, wn, p1);
            flint_mpn_zero(u, wn);
            u[2 * wn] = mpn_lshift(u + wn, w, wn, q1bits) - p1;
            mpn_tdiv_q(w, u, 2 * wn + 1, t, wn + 1);
            error++;  /* w2 is computed with 1 ulp error */
        }

        /* Do a second round of argument reduction */
        if (wp <= ARB_ATAN_TAB1_PREC)
        {
            p2 = 0;
        }
        else
        {
            q2bits = ARB_ATAN_TAB21_BITS + ARB_ATAN_TAB22_BITS;
            p2 = w[wn-1] >> (FLINT_BITS - q2bits);

            if (p2 != 0)
            {
                t[wn] = (UWORD(1) << q2bits) + mpn_mul_1(t, w, wn, p2);
                flint_mpn_zero(u, wn);
                u[2 * wn] = mpn_lshift(u + wn, w, wn, q2bits) - p2;
                mpn_tdiv_q(w, u, 2 * wn + 1, t, wn + 1);
                error++;
            }
        }

        /* |w| <= 2^-r */
        r = FLINT_BITS - FLINT_BIT_COUNT(w[wn-1]);

        /* N >= (wp-r)/(2r) */
        N = (wp - r + (2*r-1)) / (2*r);

        /* Evaluate Taylor series */
        _arb_atan_taylor_rs(t, &error2, w, wn, N, 1);

        /* Taylor series evaluation error */
        error += error2;

        /* Size of output number */
        tn = wn;

        /* First table lookup */
        if (p1 != 0)
        {
            if (wp <= ARB_ATAN_TAB1_PREC)
                mpn_add_n(t, t, arb_atan_tab1[p1] + ARB_ATAN_TAB1_LIMBS - tn, tn);
            else
                mpn_add_n(t, t, arb_atan_tab21[p1] + ARB_ATAN_TAB2_LIMBS - tn, tn);
            error++;
        }

        /* Second table lookup */
        if (p2 != 0)
        {
            mpn_add_n(t, t, arb_atan_tab22[p2] + ARB_ATAN_TAB2_LIMBS - tn, tn);
            error++;
        }

        /* pi/2 - atan(1/x) */
        if (reciprocal)
        {
            t[tn] = LIMB_ONE - mpn_sub_n(t,
                arb_atan_pi2_minus_one + ARB_ATAN_TAB2_LIMBS - tn, t, tn);

            /* result can be >= 1 */
            tn += (t[tn] != 0);

            /* error of pi/2 */
            error++;
        }

        /* The accumulated arithmetic error */
        mag_set_ui_2exp_si(arb_radref(z), error, -wn * FLINT_BITS);

        /* Truncation error from the Taylor series */
        mag_add_ui_2exp_si(arb_radref(z), arb_radref(z), 1, -r*(2*N+1));

        /* Set the midpoint */
        inexact = _arf_set_mpn_fixed(arb_midref(z), t, tn, wn, negative, prec);
        if (inexact)
            arf_mag_add_ulp(arb_radref(z), arb_radref(z), arb_midref(z), prec);

        TMP_END;
    }
}

void
arb_atan(arb_t z, const arb_t x, long prec)
{
    if (arb_is_exact(x))
    {
        arb_atan_arf(z, arb_midref(x), prec);
    }
    else
    {
        mag_t t, u;

        mag_init(t);
        mag_init(u);

        arb_get_mag_lower(t, x);

        if (mag_is_zero(t))
        {
            mag_set(t, arb_radref(x));
        }
        else
        {
            mag_mul_lower(t, t, t);
            mag_one(u);
            mag_add_lower(t, t, u);
            mag_div(t, arb_radref(x), t);
        }

        arb_atan_arf(z, arb_midref(x), prec);
        mag_add(arb_radref(z), arb_radref(z), t);

        mag_clear(t);
        mag_clear(u);
    }
}
