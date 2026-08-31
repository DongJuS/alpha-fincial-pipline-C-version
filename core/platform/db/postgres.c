#include "alpha/postgres.h"
#include "alpha/round.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libpq-fe.h>

typedef struct {
    alpha_pg_result_kind_t kind;
    alpha_pg_callback_t callback;
    void *user_data;
} pending_request_t;

struct alpha_pg {
    PGconn *conn;
    alpha_pg_state_t state;
    PostgresPollingStatusType connect_poll;
    bool flush_pending;
    bool sync_sent;
    pending_request_t *fifo;
    size_t capacity;
    size_t head;
    size_t count;
    char error[256];
};

static const char UPSERT_POSITION_SQL[] =
    "INSERT INTO portfolio_positions "
    "(ticker,name,quantity,avg_price,current_price,is_paper,account_scope,strategy_id,opened_at,"
    "updated_at) "
    "VALUES ($1,$2,$3::integer,$4::integer,$5::integer,$6::boolean,$7,NULLIF($8,''),NOW(),NOW()) "
    "ON CONFLICT (ticker,account_scope,COALESCE(strategy_id,'')) DO UPDATE SET "
    "name=EXCLUDED.name,quantity=EXCLUDED.quantity,avg_price=EXCLUDED.avg_price,"
    "current_price=EXCLUDED.current_price,is_paper=EXCLUDED.is_paper,updated_at=NOW() "
    "RETURNING "
    "ticker,name,quantity,avg_price,current_price,is_paper,account_scope,COALESCE(strategy_id,'')";

static const char EXPOSURE_SQL[] =
    "SELECT $1::text AS ticker,COALESCE(SUM(quantity),0)::bigint AS total_quantity,"
    "COALESCE(SUM(quantity*current_price),0)::bigint AS total_market_value,"
    "(SELECT COALESCE(SUM(quantity*current_price),0)::bigint FROM portfolio_positions WHERE "
    "quantity>0) AS total_aum,"
    "COUNT(DISTINCT COALESCE(strategy_id,'default'))::integer AS strategy_count "
    "FROM portfolio_positions WHERE ticker=$1 AND quantity>0";

static void set_error(alpha_pg_t *db, const char *message) {
    const char *source = message != NULL ? message : "unknown libpq error";
    (void)snprintf(db->error, sizeof(db->error), "%s", source);
}

static bool copy_text(char *dst, size_t cap, const char *src) {
    if (src == NULL || strlen(src) >= cap) {
        return false;
    }
    (void)snprintf(dst, cap, "%s", src);
    return true;
}

static bool parse_i64(const char *text, int64_t *out) {
    char *end = NULL;
    errno = 0;
    const long long value = strtoll(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    *out = (int64_t)value;
    return true;
}

static bool parse_i32(const char *text, int32_t *out) {
    int64_t value = 0;
    if (!parse_i64(text, &value) || value < INT32_MIN || value > INT32_MAX) {
        return false;
    }
    *out = (int32_t)value;
    return true;
}

alpha_err_t alpha_pg_connect_start(const char *conninfo, size_t pipeline_depth, alpha_pg_t **out) {
    if (conninfo == NULL || out == NULL || pipeline_depth < ALPHA_PG_MIN_PIPELINE_DEPTH ||
        pipeline_depth > ALPHA_PG_MAX_PIPELINE_DEPTH) {
        return ALPHA_ERR_INVALID_ARG;
    }
    *out = NULL;
    alpha_pg_t *db = calloc(1, sizeof(*db));
    if (db == NULL) {
        return ALPHA_ERR_IO;
    }
    db->fifo = calloc(pipeline_depth, sizeof(*db->fifo));
    if (db->fifo == NULL) {
        free(db);
        return ALPHA_ERR_IO;
    }
    db->capacity = pipeline_depth;
    db->state = ALPHA_PG_CONNECTING;
    db->connect_poll = PGRES_POLLING_WRITING;
    db->conn = PQconnectStart(conninfo);
    if (db->conn == NULL) {
        free(db->fifo);
        free(db);
        return ALPHA_ERR_DB;
    }
    if (PQstatus(db->conn) == CONNECTION_BAD || PQsetnonblocking(db->conn, 1) != 0) {
        set_error(db, PQerrorMessage(db->conn));
        db->state = ALPHA_PG_FAILED;
    }
    *out = db;
    return db->state == ALPHA_PG_FAILED ? ALPHA_ERR_DB : ALPHA_OK;
}

void alpha_pg_close(alpha_pg_t *db) {
    if (db == NULL) {
        return;
    }
    PQfinish(db->conn);
    free(db->fifo);
    free(db);
}

alpha_pg_state_t alpha_pg_state(const alpha_pg_t *db) {
    return db != NULL ? db->state : ALPHA_PG_FAILED;
}

int alpha_pg_socket(const alpha_pg_t *db) { return db != NULL ? PQsocket(db->conn) : -1; }

bool alpha_pg_wants_read(const alpha_pg_t *db) {
    if (db == NULL) {
        return false;
    }
    if (db->state == ALPHA_PG_CONNECTING && db->connect_poll == PGRES_POLLING_READING) {
        return true;
    }
    if (db->state == ALPHA_PG_READY && (db->count > 0 || db->sync_sent)) {
        return true;
    }
    return false;
}

bool alpha_pg_wants_write(const alpha_pg_t *db) {
    if (db == NULL) {
        return false;
    }
    if (db->state == ALPHA_PG_CONNECTING && db->connect_poll == PGRES_POLLING_WRITING) {
        return true;
    }
    if (db->state == ALPHA_PG_READY && (db->flush_pending || (db->count > 0 && !db->sync_sent))) {
        return true;
    }
    return false;
}

size_t alpha_pg_pending(const alpha_pg_t *db) { return db != NULL ? db->count : 0; }
size_t alpha_pg_capacity(const alpha_pg_t *db) { return db != NULL ? db->capacity : 0; }
bool alpha_pg_is_idle(const alpha_pg_t *db) {
    if (db == NULL || db->count != 0 || db->sync_sent || db->flush_pending) {
        return false;
    }
    return true;
}
const char *alpha_pg_error(const alpha_pg_t *db) { return db != NULL ? db->error : "invalid db"; }

static void complete_head(alpha_pg_t *db, alpha_pg_result_t *result) {
    pending_request_t request = db->fifo[db->head];
    db->head = (db->head + 1) % db->capacity;
    db->count--;
    if (request.callback != NULL) {
        request.callback(result, request.user_data);
    }
}

static bool parse_position(PGresult *pg, alpha_pg_result_t *result) {
    alpha_pg_position_t *p = &result->value.position;
    if (PQntuples(pg) != 1 || !copy_text(p->ticker, sizeof(p->ticker), PQgetvalue(pg, 0, 0)) ||
        !copy_text(p->name, sizeof(p->name), PQgetvalue(pg, 0, 1)) ||
        !parse_i32(PQgetvalue(pg, 0, 2), &p->quantity) ||
        !parse_i32(PQgetvalue(pg, 0, 3), &p->avg_price) ||
        !parse_i32(PQgetvalue(pg, 0, 4), &p->current_price) ||
        !copy_text(p->account_scope, sizeof(p->account_scope), PQgetvalue(pg, 0, 6)) ||
        !copy_text(p->strategy_id, sizeof(p->strategy_id), PQgetvalue(pg, 0, 7))) {
        return false;
    }
    p->is_paper = strcmp(PQgetvalue(pg, 0, 5), "t") == 0;
    return true;
}

static bool parse_exposure(PGresult *pg, alpha_pg_result_t *result) {
    alpha_pg_exposure_t *e = &result->value.exposure;
    if (PQntuples(pg) != 1 || !copy_text(e->ticker, sizeof(e->ticker), PQgetvalue(pg, 0, 0)) ||
        !parse_i64(PQgetvalue(pg, 0, 1), &e->total_quantity) ||
        !parse_i64(PQgetvalue(pg, 0, 2), &e->total_market_value) ||
        !parse_i64(PQgetvalue(pg, 0, 3), &e->total_aum) ||
        !parse_i32(PQgetvalue(pg, 0, 4), &e->strategy_count)) {
        return false;
    }
    const double unrounded =
        e->total_aum > 0 ? (double)e->total_market_value / (double)e->total_aum * 100.0 : 0.0;
    e->exposure_pct = alpha_round_dp(unrounded, 2);
    return true;
}

static alpha_err_t drain_results(alpha_pg_t *db) {
    while (!PQisBusy(db->conn)) {
        PGresult *pg = PQgetResult(db->conn);
        if (pg == NULL) {
            /* In pipeline mode libpq may emit a NULL query boundary while
             * additional queued results are already available. Re-check
             * PQisBusy instead of waiting for a socket edge that will not
             * recur for buffered input. */
            if (db->count > 0 || db->sync_sent) {
                continue;
            }
            break;
        }
        const ExecStatusType status = PQresultStatus(pg);
        if (status == PGRES_PIPELINE_SYNC) {
            db->sync_sent = false;
            PQclear(pg);
            continue;
        }
        if (db->count == 0) {
            set_error(db, "libpq result arrived without a queued request");
            PQclear(pg);
            db->state = ALPHA_PG_FAILED;
            return ALPHA_ERR_DB;
        }
        alpha_pg_result_t result = {.kind = db->fifo[db->head].kind, .status = ALPHA_OK};
        bool parsed = false;
        if (status == PGRES_TUPLES_OK) {
            if (result.kind == ALPHA_PG_RESULT_POSITION) {
                parsed = parse_position(pg, &result);
            } else {
                parsed = parse_exposure(pg, &result);
            }
        }
        if (!parsed) {
            result.status = ALPHA_ERR_DB;
            set_error(db, status == PGRES_PIPELINE_ABORTED
                              ? "request aborted by an earlier pipeline statement"
                              : PQresultErrorMessage(pg));
        }
        PQclear(pg);
        complete_head(db, &result);
    }
    return ALPHA_OK;
}

static alpha_err_t service_connect(alpha_pg_t *db, bool readable, bool writable) {
    bool expected = false;
    if (db->connect_poll == PGRES_POLLING_READING && readable) {
        expected = true;
    }
    if (db->connect_poll == PGRES_POLLING_WRITING && writable) {
        expected = true;
    }
    if (!expected) {
        return ALPHA_OK;
    }
    db->connect_poll = PQconnectPoll(db->conn);
    if (db->connect_poll == PGRES_POLLING_FAILED) {
        set_error(db, PQerrorMessage(db->conn));
        db->state = ALPHA_PG_FAILED;
        return ALPHA_ERR_DB;
    }
    if (db->connect_poll == PGRES_POLLING_OK && PQenterPipelineMode(db->conn) != 1) {
        set_error(db, PQerrorMessage(db->conn));
        db->state = ALPHA_PG_FAILED;
        return ALPHA_ERR_DB;
    }
    if (db->connect_poll == PGRES_POLLING_OK) {
        db->state = ALPHA_PG_READY;
    }
    return ALPHA_OK;
}

alpha_err_t alpha_pg_service(alpha_pg_t *db, bool readable, bool writable) {
    if (db == NULL || db->state == ALPHA_PG_FAILED) {
        return ALPHA_ERR_INVALID_ARG;
    }
    if (db->state == ALPHA_PG_CONNECTING) {
        return service_connect(db, readable, writable);
    }
    if (writable && db->count > 0 && !db->sync_sent) {
        if (PQpipelineSync(db->conn) != 1) {
            set_error(db, PQerrorMessage(db->conn));
            db->state = ALPHA_PG_FAILED;
            return ALPHA_ERR_DB;
        }
        db->sync_sent = true;
        db->flush_pending = true;
    }
    if (writable && db->flush_pending) {
        const int flushed = PQflush(db->conn);
        if (flushed < 0) {
            set_error(db, PQerrorMessage(db->conn));
            return ALPHA_ERR_DB;
        }
        db->flush_pending = flushed != 0;
    }
    if (readable) {
        if (PQconsumeInput(db->conn) != 1) {
            set_error(db, PQerrorMessage(db->conn));
            return ALPHA_ERR_DB;
        }
        return drain_results(db);
    }
    return ALPHA_OK;
}

static alpha_err_t send_request(alpha_pg_t *db, alpha_pg_result_kind_t kind, const char *sql,
                                int nparams, const char *const *params,
                                alpha_pg_callback_t callback, void *user_data) {
    if (db == NULL || sql == NULL || db->state != ALPHA_PG_READY) {
        return ALPHA_ERR_INVALID_ARG;
    }
    if (db->count == db->capacity) {
        return ALPHA_ERR_RANGE;
    }
    if (db->sync_sent) {
        return ALPHA_ERR_RANGE;
    }
    if (PQsendQueryParams(db->conn, sql, nparams, NULL, params, NULL, NULL, 0) != 1) {
        set_error(db, PQerrorMessage(db->conn));
        return ALPHA_ERR_DB;
    }
    const size_t tail = (db->head + db->count) % db->capacity;
    db->fifo[tail] =
        (pending_request_t){.kind = kind, .callback = callback, .user_data = user_data};
    db->count++;
    db->flush_pending = PQflush(db->conn) != 0;
    return ALPHA_OK;
}

alpha_err_t alpha_pg_upsert_position(alpha_pg_t *db, const alpha_pg_position_t *position,
                                     alpha_pg_callback_t callback, void *user_data) {
    if (position == NULL || position->ticker[0] == '\0' || position->name[0] == '\0' ||
        position->quantity <= 0 || position->account_scope[0] == '\0') {
        return ALPHA_ERR_INVALID_ARG;
    }
    char quantity[16];
    char avg_price[16];
    char current_price[16];
    (void)snprintf(quantity, sizeof(quantity), "%d", position->quantity);
    (void)snprintf(avg_price, sizeof(avg_price), "%d", position->avg_price);
    (void)snprintf(current_price, sizeof(current_price), "%d", position->current_price);
    const char *params[] = {
        position->ticker,
        position->name,
        quantity,
        avg_price,
        current_price,
        position->is_paper ? "true" : "false",
        position->account_scope,
        position->strategy_id,
    };
    return send_request(db, ALPHA_PG_RESULT_POSITION, UPSERT_POSITION_SQL, 8, params, callback,
                        user_data);
}

alpha_err_t alpha_pg_query_exposure(alpha_pg_t *db, const char *ticker,
                                    alpha_pg_callback_t callback, void *user_data) {
    if (ticker == NULL || ticker[0] == '\0' || strlen(ticker) >= ALPHA_PG_TEXT_CAP) {
        return ALPHA_ERR_INVALID_ARG;
    }
    const char *params[] = {ticker};
    return send_request(db, ALPHA_PG_RESULT_EXPOSURE, EXPOSURE_SQL, 1, params, callback, user_data);
}
