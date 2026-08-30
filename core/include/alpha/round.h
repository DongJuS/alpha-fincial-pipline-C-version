#ifndef ALPHA_ROUND_H
#define ALPHA_ROUND_H

/* Python-compatible decimal rounding (round-half-to-even).
 *
 * Python `round(x, ndigits)` returns the double nearest to the value of x
 * rounded to `ndigits` decimal places with ties going to the even digit.
 * With the FPU in FE_TONEAREST (set by alpha_initialize), the C library's
 * `snprintf("%.*f")` performs the same correctly-rounded, ties-to-even decimal
 * conversion, and `strtod` returns the nearest double to that decimal string.
 * This reproduces Python's result; plain C `round()` does not (it rounds half
 * away from zero and only to integers). */
double alpha_round_dp(double value, int ndigits);

#endif
