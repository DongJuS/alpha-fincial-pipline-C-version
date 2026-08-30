#ifndef ALPHA_ERRORS_H
#define ALPHA_ERRORS_H

typedef enum {
    ALPHA_OK = 0,
    ALPHA_ERR_INVALID_ARG,
    ALPHA_ERR_RANGE,
    ALPHA_ERR_IO,
    ALPHA_ERR_DB,
    ALPHA_ERR_HTTP,
} alpha_err_t;

#endif
