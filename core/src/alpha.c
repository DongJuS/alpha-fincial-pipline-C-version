#include "alpha/alpha.h"

#include <fenv.h>

alpha_err_t alpha_initialize(void) {
    if (fesetround(FE_TONEAREST) != 0 || fegetround() != FE_TONEAREST) {
        return ALPHA_ERR_RANGE;
    }
    return ALPHA_OK;
}

int alpha_abi_version(void) { return ALPHA_ABI_VERSION; }
