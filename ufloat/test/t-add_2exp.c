/*=============================================================================

    This file is part of ARB.

    ARB is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    ARB is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with ARB; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA

=============================================================================*/
/******************************************************************************

    Copyright (C) 2012 Fredrik Johansson

******************************************************************************/

#include "ufloat.h"

int main()
{
    long iter;
    flint_rand_t state;

    printf("add_2exp....");
    fflush(stdout);

    flint_randinit(state);

    for (iter = 0; iter < 1000000; iter++)
    {
        fmpz_t x, y, xy, z;
        ufloat_t u, w;
        long exp;

        fmpz_init(x);
        fmpz_init(y);
        fmpz_init(xy);
        fmpz_init(z);

        fmpz_randtest_not_zero(x, state, 1 + n_randint(state, 500));
        exp =  n_randint(state, 500);
        fmpz_set_ui(y, 1UL);
        fmpz_mul_2exp(y, y, exp);

        ufloat_set_fmpz(u, x);
        ufloat_add_2exp(w, u, exp);

        fmpz_add(xy, x, y);

        ufloat_get_fmpz(z, w);

        if (fmpz_cmpabs(z, xy) < 0)
        {
            printf("fail!\n");
            printf("x = "); fmpz_print(x); printf("\n\n");
            printf("y = "); fmpz_print(y); printf("\n\n");
            printf("xy = "); fmpz_print(xy); printf("\n\n");
            printf("z = "); fmpz_print(z); printf("\n\n");
            abort();
        }

        if (FLINT_BIT_COUNT(w->man) != UFLOAT_PREC)
        {
            printf("wrong number of bits!\n");
            abort();
        }

        fmpz_clear(x);
        fmpz_clear(y);
        fmpz_clear(xy);
        fmpz_clear(z);
    }

    flint_randclear(state);
    _fmpz_cleanup();
    printf("PASS\n");
    return EXIT_SUCCESS;
}

