#ifndef ALPHA_ALPHA_H
#define ALPHA_ALPHA_H

#include "alpha/constants.h"
#include "alpha/domain.h"
#include "alpha/errors.h"

#define ALPHA_ABI_VERSION 1

alpha_err_t alpha_initialize(void);
int alpha_abi_version(void);

#endif
