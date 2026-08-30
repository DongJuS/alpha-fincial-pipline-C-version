#include "alpha/round.h"

#include <stdio.h>
#include <stdlib.h>

double alpha_round_dp(double value, int ndigits) {
    if (ndigits < 0) {
        ndigits = 0;
    }
    /* snprintf with FE_TONEAREST rounds the decimal representation
     * half-to-even, matching CPython's round(); strtod returns the nearest
     * double to that decimal string. A 32-byte buffer holds any double with
     * up to ~17 significant digits plus a small fractional field. */
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.*f", ndigits, value);
    return strtod(buffer, NULL);
}
