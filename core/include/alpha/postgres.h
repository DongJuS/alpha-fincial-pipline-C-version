#ifndef ALPHA_POSTGRES_H
#define ALPHA_POSTGRES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "alpha/errors.h"

/* Readiness-driven libpq client. The caller owns the event loop and calls
 * alpha_pg_service() only when alpha_pg_socket() becomes readable/writable. */

#define ALPHA_PG_TEXT_CAP 64
#define ALPHA_PG_MIN_PIPELINE_DEPTH 1
#define ALPHA_PG_MAX_PIPELINE_DEPTH 64

typedef struct alpha_pg alpha_pg_t;

typedef enum {
    ALPHA_PG_CONNECTING = 0,
    ALPHA_PG_READY,
    ALPHA_PG_FAILED,
} alpha_pg_state_t;

typedef struct {
    char ticker[ALPHA_PG_TEXT_CAP];
    char name[ALPHA_PG_TEXT_CAP];
    int32_t quantity;
    int32_t avg_price;
    int32_t current_price;
    bool is_paper;
    char account_scope[ALPHA_PG_TEXT_CAP];
    char strategy_id[ALPHA_PG_TEXT_CAP];
} alpha_pg_position_t;

typedef struct {
    char ticker[ALPHA_PG_TEXT_CAP];
    int64_t total_quantity;
    int64_t total_market_value;
    int64_t total_aum;
    int32_t strategy_count;
    double exposure_pct;
} alpha_pg_exposure_t;

typedef enum {
    ALPHA_PG_RESULT_POSITION = 0,
    ALPHA_PG_RESULT_EXPOSURE,
} alpha_pg_result_kind_t;

typedef struct {
    alpha_pg_result_kind_t kind;
    alpha_err_t status;
    union {
        alpha_pg_position_t position;
        alpha_pg_exposure_t exposure;
    } value;
} alpha_pg_result_t;

typedef void (*alpha_pg_callback_t)(const alpha_pg_result_t *result, void *user_data);

/* Starts a nonblocking connection using PQconnectStart/PQconnectPoll. No socket
 * wait is performed here. pipeline_depth bounds both sent work and result FIFO. */
alpha_err_t alpha_pg_connect_start(const char *conninfo, size_t pipeline_depth, alpha_pg_t **out);
void alpha_pg_close(alpha_pg_t *db);

alpha_pg_state_t alpha_pg_state(const alpha_pg_t *db);
int alpha_pg_socket(const alpha_pg_t *db);
bool alpha_pg_wants_read(const alpha_pg_t *db);
bool alpha_pg_wants_write(const alpha_pg_t *db);
size_t alpha_pg_pending(const alpha_pg_t *db);
size_t alpha_pg_capacity(const alpha_pg_t *db);
/* True only after both request results and the pipeline sync boundary drain. */
bool alpha_pg_is_idle(const alpha_pg_t *db);
const char *alpha_pg_error(const alpha_pg_t *db);

/* Advances connection polling, output flushing, and result draining. readable
 * and writable describe the readiness delivered by the owner's event loop. */
alpha_err_t alpha_pg_service(alpha_pg_t *db, bool readable, bool writable);

/* Python save_position parity for positive quantities. The returned row is the
 * database round-trip value. quantity <= 0 is rejected rather than silently
 * changing this typed operation into Python's DELETE branch. */
alpha_err_t alpha_pg_upsert_position(alpha_pg_t *db, const alpha_pg_position_t *position,
                                     alpha_pg_callback_t callback, void *user_data);

/* Python AggregateRiskMonitor.check_total_exposure parity. Only quantity > 0
 * contributes. total_aum spans all tickers; NULL strategy_id is "default". */
alpha_err_t alpha_pg_query_exposure(alpha_pg_t *db, const char *ticker,
                                    alpha_pg_callback_t callback, void *user_data);

#endif
