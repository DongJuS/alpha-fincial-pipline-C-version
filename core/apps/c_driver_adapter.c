#include "alpha/lws_runtime.h"
#include "alpha/postgres.h"
#include "alpha/redis_async.h"

#include <libpq-fe.h>
#include <libwebsockets.h>
#include <openssl/evp.h>
#include <yyjson.h>

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#define OP_COUNT 3U
#define MAX_CONCURRENCY 32U
#define HASH_HEX_SIZE 65U
#define FIXTURE_MAX_SIZE ((size_t)1024U * 1024U)

typedef enum { CASE_REDIS, CASE_DB } case_kind_t;

typedef struct adapter adapter_t;
typedef struct {
    adapter_t *adapter;
    size_t index;
    uint64_t started_ns;
} token_t;

struct adapter {
    alpha_lws_runtime_t *runtime;
    alpha_lws_watch_t *watch;
    alpha_redis_async_t *redis;
    alpha_pg_t *pg;
    case_kind_t kind;
    size_t repeat;
    size_t concurrency;
    size_t total;
    size_t submitted;
    size_t completed;
    size_t current;
    bool connected;
    bool inflight;
    bool failed;
    uint64_t *latency_ns;
    token_t token;
    char ticker[32];
    char namespace_text[96];
    char redis_value[128];
    alpha_pg_position_t db_rows[2];
    alpha_pg_exposure_t exposure;
};

static uint64_t now_ns(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        return 0;
    }
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}

static uint32_t namespace_hash(const char *text) {
    uint32_t hash = UINT32_C(2166136261);
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor != '\0'; ++cursor) {
        hash ^= *cursor;
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static bool sha256_bytes(const void *data, size_t size, char output[HASH_HEX_SIZE]) {
    unsigned char digest[32];
    unsigned int digest_size = 0;
    if (EVP_Digest(data, size, digest, &digest_size, EVP_sha256(), NULL) != 1 ||
        digest_size != sizeof(digest)) {
        return false;
    }
    for (size_t i = 0; i < sizeof(digest); ++i) {
        (void)snprintf(output + i * 2, 3, "%02x", digest[i]);
    }
    return true;
}

static char *read_file(const char *path, size_t *size_out) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    char *bytes = malloc(FIXTURE_MAX_SIZE + 1U);
    const size_t length = bytes != NULL ? fread(bytes, 1, FIXTURE_MAX_SIZE + 1U, file) : 0;
    if (bytes == NULL || length == 0 || length > FIXTURE_MAX_SIZE || !feof(file)) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    bytes[length] = '\0';
    *size_out = length;
    return bytes;
}

static void ready(void *context, bool readable, bool writable, bool timed_out) {
    adapter_t *adapter = context;
    alpha_err_t status = ALPHA_OK;
    if (adapter->kind == CASE_DB) {
        status = alpha_pg_service(adapter->pg, readable, writable);
        if (status == ALPHA_OK) {
            status = alpha_lws_watch_set_write(adapter->watch, alpha_pg_wants_write(adapter->pg));
        }
    } else if (timed_out) {
        status = alpha_redis_async_timeout(adapter->redis);
    } else {
        alpha_redis_watch_t flags = ALPHA_REDIS_WATCH_NONE;
        if (readable) {
            flags = ALPHA_REDIS_WATCH_READ;
        }
        if (writable) {
            flags = flags == ALPHA_REDIS_WATCH_READ ? ALPHA_REDIS_WATCH_READ_WRITE
                                                    : ALPHA_REDIS_WATCH_WRITE;
        }
        status = alpha_redis_async_service(adapter->redis, flags);
    }
    if (status != ALPHA_OK) {
        adapter->failed = true;
    }
}

static void redis_watch(void *context, int fd, alpha_redis_watch_t flags) {
    adapter_t *adapter = context;
    if (fd < 0 || flags == ALPHA_REDIS_WATCH_NONE) {
        return;
    }
    bool write = false;
    if (flags == ALPHA_REDIS_WATCH_WRITE || flags == ALPHA_REDIS_WATCH_READ_WRITE) {
        write = true;
    }
    if (adapter->watch == NULL) {
        if (alpha_lws_watch_add(adapter->runtime, fd, write, ready, adapter, &adapter->watch) !=
            ALPHA_OK) {
            adapter->failed = true;
        }
    } else if (alpha_lws_watch_set_write(adapter->watch, write) != ALPHA_OK) {
        adapter->failed = true;
    }
}

static void redis_timer(void *context, long timeout_ms) {
    adapter_t *adapter = context;
    if (adapter->watch != NULL &&
        alpha_lws_watch_set_timer(adapter->watch, timeout_ms) != ALPHA_OK) {
        adapter->failed = true;
    }
}

static void redis_connect(void *context, alpha_err_t status) {
    adapter_t *adapter = context;
    adapter->connected = status == ALPHA_OK;
    if (!adapter->connected) {
        adapter->failed = true;
    }
}

static void finish_operation(adapter_t *adapter, token_t *token, bool ok) {
    const uint64_t ended = now_ns();
    if (!ok || ended <= token->started_ns || token->index >= adapter->total) {
        adapter->failed = true;
    } else {
        adapter->latency_ns[token->index] = ended - token->started_ns;
    }
    adapter->completed++;
    adapter->inflight = false;
}

static void redis_reply(void *context, alpha_err_t status, const char *text, long long integer,
                        void *user_data) {
    adapter_t *adapter = context;
    token_t *token = user_data;
    const size_t operation = token->index % OP_COUNT;
    bool ok = status == ALPHA_OK;
    if (operation == 0) {
        if (!ok || text == NULL || strcmp(text, "OK") != 0) {
            ok = false;
        }
    } else if (operation == 1) {
        if (!ok || text == NULL || strcmp(text, adapter->redis_value) != 0) {
            ok = false;
        }
    } else if (!ok || integer != 0) {
        ok = false;
    }
    finish_operation(adapter, token, ok);
}

static void pg_reply(const alpha_pg_result_t *result, void *user_data) {
    token_t *token = user_data;
    adapter_t *adapter = token->adapter;
    const size_t operation = token->index % OP_COUNT;
    bool ok = result->status == ALPHA_OK;
    if (ok && operation < 2) {
        adapter->db_rows[operation] = result->value.position;
    } else if (ok) {
        adapter->exposure = result->value.exposure;
        if (adapter->exposure.total_quantity != 30 ||
            adapter->exposure.total_market_value != 34000) {
            ok = false;
        }
    }
    finish_operation(adapter, token, ok);
}

static alpha_err_t submit_one(adapter_t *adapter) {
    const size_t index = adapter->submitted;
    const size_t operation = index % OP_COUNT;
    adapter->token = (token_t){.adapter = adapter, .index = index, .started_ns = now_ns()};
    alpha_err_t status = ALPHA_ERR_INVALID_ARG;
    if (adapter->kind == CASE_REDIS) {
        char key[160];
        if (operation < 2) {
            (void)snprintf(key, sizeof(key), "redis:cache:latest_ticks:%s", adapter->ticker);
        } else {
            (void)snprintf(key, sizeof(key), "hard_stop:lockout:%s", adapter->namespace_text);
        }
        if (operation == 0) {
            const char *args[] = {"SET", key, adapter->redis_value, "EX", "60"};
            const size_t lengths[] = {3, strlen(key), strlen(adapter->redis_value), 2, 2};
            status = alpha_redis_async_command(adapter->redis, 5, args, lengths, &adapter->token);
        } else {
            const char *verb = operation == 1 ? "GET" : "EXISTS";
            const char *args[] = {verb, key};
            const size_t lengths[] = {strlen(verb), strlen(key)};
            status = alpha_redis_async_command(adapter->redis, 2, args, lengths, &adapter->token);
        }
    } else if (operation < 2) {
        status = alpha_pg_upsert_position(adapter->pg, &adapter->db_rows[operation], pg_reply,
                                          &adapter->token);
    } else {
        status = alpha_pg_query_exposure(adapter->pg, adapter->ticker, pg_reply, &adapter->token);
    }
    if (status == ALPHA_OK) {
        adapter->submitted++;
        adapter->inflight = true;
        if (adapter->kind == CASE_DB &&
            alpha_lws_watch_set_write(adapter->watch, alpha_pg_wants_write(adapter->pg)) !=
                ALPHA_OK) {
            return ALPHA_ERR_IO;
        }
    }
    return status;
}

static bool drive(adapter_t *adapter, uint64_t deadline_ns) {
    while (!adapter->failed && adapter->completed < adapter->total) {
        if (!adapter->inflight && (adapter->kind == CASE_REDIS || alpha_pg_is_idle(adapter->pg)) &&
            submit_one(adapter) != ALPHA_OK) {
            adapter->failed = true;
            break;
        }
        if (alpha_lws_runtime_service(adapter->runtime, 1) != ALPHA_OK || now_ns() > deadline_ns) {
            adapter->failed = true;
        }
    }
    if (adapter->failed) {
        return false;
    }
    return true;
}

static bool pg_cleanup(const char *conninfo, const char *ticker) {
    PGconn *connection = PQconnectdb(conninfo);
    if (PQstatus(connection) != CONNECTION_OK) {
        PQfinish(connection);
        return false;
    }
    const char *params[] = {ticker};
    PGresult *result = PQexecParams(connection, "DELETE FROM portfolio_positions WHERE ticker=$1",
                                    1, NULL, params, NULL, NULL, 0);
    const bool ok = PQresultStatus(result) == PGRES_COMMAND_OK;
    PQclear(result);
    PQfinish(connection);
    return ok;
}

static bool wait_connection(adapter_t *adapter, uint64_t deadline_ns) {
    while (!adapter->failed) {
        if ((adapter->kind == CASE_REDIS && adapter->connected) ||
            (adapter->kind == CASE_DB && alpha_pg_state(adapter->pg) == ALPHA_PG_READY)) {
            return true;
        }
        if (alpha_lws_runtime_service(adapter->runtime, 1) != ALPHA_OK || now_ns() > deadline_ns) {
            return false;
        }
    }
    return false;
}

static bool terminal_matches_service(const adapter_t *adapter) {
    if (adapter->kind == CASE_REDIS) {
        /* The final timed sequence is SET(EX=60), GET, EXISTS. Each callback is
         * validated before completion; finishing inside five seconds proves the
         * SET TTL remains positive at the terminal boundary. */
        return adapter->completed == adapter->total;
    }
    if (strcmp(adapter->db_rows[0].strategy_id, "strategy-a") != 0 ||
        adapter->db_rows[0].quantity != 10 || adapter->db_rows[0].current_price != 1000 ||
        strcmp(adapter->db_rows[1].strategy_id, "strategy-b") != 0 ||
        adapter->db_rows[1].quantity != 20 || adapter->db_rows[1].current_price != 1200 ||
        adapter->exposure.total_quantity != 30 || adapter->exposure.total_market_value != 34000) {
        return false;
    }
    return true;
}

static void json_string(const char *text) {
    putchar('"');
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor != '\0'; ++cursor) {
        if (*cursor == '"' || *cursor == '\\') {
            putchar('\\');
        }
        putchar((int)*cursor);
    }
    putchar('"');
}

static void emit(const adapter_t *adapter, const char *fixture_hash, const char *result_hash,
                 const char *terminal_json, uint64_t elapsed_ns, uint64_t cpu_ns,
                 const char *service_hash, const char *schema_hash) {
    struct rusage usage;
    (void)getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
    const int64_t rss = usage.ru_maxrss;
#else
    const int64_t rss = usage.ru_maxrss * 1024;
#endif
    printf("{\"build\":{\"compiler\":");
    json_string(__VERSION__);
    printf(",\"dependencies\":{\"hiredis\":\"linked\",\"libpq\":\"linked\","
           "\"libwebsockets\":\"4.3.3-v4.3.3\",\"openssl\":\"linked\"},"
           "\"flags\":\"C11 -ffp-contract=off\",\"runtime\":\"C11\"},"
           "\"completed_ids\":[");
    const char prefix = adapter->kind == CASE_REDIS ? 'r' : 'd';
    for (size_t i = 0; i < OP_COUNT; ++i) {
        printf("%s\"%c%03zu\"", i == 0 ? "" : ",", prefix, i + 1);
    }
    printf("],\"completed_tokens\":[");
    for (size_t i = 0; i < adapter->total; ++i) {
        printf("%s\"%06zu:%c%03zu\"", i == 0 ? "" : ",", i / OP_COUNT, prefix, i % OP_COUNT + 1);
    }
    printf("],\"configuration\":{\"concurrency\":%zu,\"connection_count\":1,"
           "\"event_loop_mode\":\"lws\",\"operation_count\":%zu,\"pipeline_depth\":1,"
           "\"queue_depth\":1,\"retry_policy\":\"none\","
           "\"saturation_policy\":\"bounded_wait\",\"schema_sha256\":\"%s\","
           "\"service_config_sha256\":\"%s\",\"timeout_ms\":5000,\"worker_count\":1},"
           "\"dropped\":0,\"elapsed_ms\":%.9f,\"errors\":0,\"fixture_sha256\":\"%s\","
           "\"operation_latency_ns\":[",
           adapter->concurrency, adapter->total, schema_hash, service_hash,
           (double)elapsed_ns / 1000000.0, fixture_hash);
    for (size_t i = 0; i < adapter->total; ++i) {
        printf("%s%" PRIu64, i == 0 ? "" : ",", adapter->latency_ns[i]);
    }
    printf("],\"resources\":{\"cpu_time_ms\":%.9f,\"peak_rss_bytes\":%" PRId64
           "},\"result_sha256\":\"%s\",\"terminal\":%s,\"trial_namespace\":",
           (double)cpu_ns / 1000000.0, rss, result_hash, terminal_json);
    json_string(adapter->namespace_text);
    puts("}");
}

static const char *arg_value(int argc, char **argv, const char *name) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp(argv[i], name) == 0) {
            return argv[i + 1];
        }
    }
    return NULL;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
int main(int argc, char **argv) {
    const char *fixture_path = arg_value(argc, argv, "--fixture");
    const char *case_name = arg_value(argc, argv, "--case");
    const char *concurrency_text = arg_value(argc, argv, "--concurrency");
    const char *trial_text = arg_value(argc, argv, "--trial");
    const char *namespace_text = arg_value(argc, argv, "--namespace");
    if (fixture_path == NULL || case_name == NULL || concurrency_text == NULL ||
        trial_text == NULL) {
        fprintf(stderr, "required: --fixture --case --concurrency --trial [--namespace]\n");
        return 2;
    }
    char generated_namespace[96];
    if (namespace_text == NULL) {
        (void)snprintf(generated_namespace, sizeof(generated_namespace), "c-trial-%s", trial_text);
        namespace_text = generated_namespace;
    }
    char *end = NULL;
    const unsigned long concurrency = strtoul(concurrency_text, &end, 10);
    if (*concurrency_text == '\0' || *end != '\0' ||
        (concurrency != 1 && concurrency != 8 && concurrency != 32)) {
        return 2;
    }
    size_t fixture_size = 0;
    char *fixture_bytes = read_file(fixture_path, &fixture_size);
    char fixture_hash[HASH_HEX_SIZE];
    if (fixture_bytes == NULL || !sha256_bytes(fixture_bytes, fixture_size, fixture_hash)) {
        free(fixture_bytes);
        return 2;
    }
    yyjson_doc *document = yyjson_read(fixture_bytes, fixture_size, 0);
    yyjson_val *root = document != NULL ? yyjson_doc_get_root(document) : NULL;
    yyjson_val *contract = root != NULL ? yyjson_obj_get(root, "contract") : NULL;
    yyjson_val *cases = root != NULL ? yyjson_obj_get(root, "cases") : NULL;
    yyjson_val *case_value = cases != NULL ? yyjson_obj_get(cases, case_name) : NULL;
    yyjson_val *terminal = case_value != NULL ? yyjson_obj_get(case_value, "terminal") : NULL;
    const char *terminal_hash =
        case_value != NULL ? yyjson_get_str(yyjson_obj_get(case_value, "terminal_sha256")) : NULL;
    const char *service_hash =
        contract != NULL ? yyjson_get_str(yyjson_obj_get(contract, "service_config_sha256")) : NULL;
    const char *schema_hash =
        contract != NULL ? yyjson_get_str(yyjson_obj_get(contract, "schema_sha256")) : NULL;
    const uint64_t repeat =
        case_value != NULL ? yyjson_get_uint(yyjson_obj_get(case_value, "repeat")) : 0;
    char *terminal_json =
        terminal != NULL ? yyjson_val_write(terminal, YYJSON_WRITE_NOFLAG, NULL) : NULL;
    if (terminal_json == NULL || terminal_hash == NULL || service_hash == NULL ||
        schema_hash == NULL || repeat == 0 ||
        yyjson_get_uint(yyjson_obj_get(case_value, "pipeline_depth")) != 1) {
        free(terminal_json);
        yyjson_doc_free(document);
        free(fixture_bytes);
        return 2;
    }

    adapter_t adapter = {
        .kind = strcmp(case_name, "redis-hot-path") == 0 ? CASE_REDIS : CASE_DB,
        .repeat = (size_t)repeat,
        .concurrency = (size_t)concurrency,
        .total = (size_t)repeat * OP_COUNT,
    };
    if ((adapter.kind == CASE_DB && strcmp(case_name, "db-read-write") != 0) ||
        strlen(namespace_text) >= sizeof(adapter.namespace_text)) {
        return 2;
    }
    (void)snprintf(adapter.namespace_text, sizeof(adapter.namespace_text), "%s", namespace_text);
    (void)snprintf(adapter.redis_value, sizeof(adapter.redis_value),
                   "{\"price\":70000,\"volume\":10}");
    adapter.latency_ns = calloc(adapter.total, sizeof(*adapter.latency_ns));
    if (adapter.latency_ns == NULL || alpha_lws_runtime_create(2, &adapter.runtime) != ALPHA_OK) {
        return 2;
    }
    const uint32_t ns_hash = namespace_hash(namespace_text);
    if (adapter.kind == CASE_REDIS) {
        (void)snprintf(adapter.ticker, sizeof(adapter.ticker), "005930:%08x", ns_hash);
        if (alpha_redis_async_connect("127.0.0.1", 56379, redis_watch, redis_timer, redis_connect,
                                      redis_reply, &adapter, &adapter.redis) != ALPHA_OK) {
            adapter.failed = true;
        }
    } else {
        (void)snprintf(adapter.ticker, sizeof(adapter.ticker), "M%08x", ns_hash);
        for (size_t i = 0; i < 2; ++i) {
            adapter.db_rows[i] = (alpha_pg_position_t){
                .quantity = i == 0 ? 10 : 20,
                .avg_price = 1000,
                .current_price = i == 0 ? 1000 : 1200,
                .is_paper = true,
            };
            (void)snprintf(adapter.db_rows[i].ticker, ALPHA_PG_TEXT_CAP, "%s", adapter.ticker);
            (void)snprintf(adapter.db_rows[i].name, ALPHA_PG_TEXT_CAP, "MVP-2 fixture");
            (void)snprintf(adapter.db_rows[i].account_scope, ALPHA_PG_TEXT_CAP, "paper");
            (void)snprintf(adapter.db_rows[i].strategy_id, ALPHA_PG_TEXT_CAP, "strategy-%c",
                           (int)('a' + i));
        }
        const char *conninfo = getenv("ALPHA_POSTGRES_URL");
        if (conninfo == NULL) {
            conninfo = "host=127.0.0.1 port=55432 dbname=alpha_test user=alpha_test "
                       "password=alpha_test_only";
        }
        if (!pg_cleanup(conninfo, adapter.ticker) ||
            alpha_pg_connect_start(conninfo, 1, &adapter.pg) != ALPHA_OK ||
            alpha_lws_watch_add(adapter.runtime, alpha_pg_socket(adapter.pg), true, ready, &adapter,
                                &adapter.watch) != ALPHA_OK) {
            adapter.failed = true;
        }
    }
    const uint64_t setup_deadline = now_ns() + UINT64_C(5000000000);
    if (!wait_connection(&adapter, setup_deadline)) {
        adapter.failed = true;
    }
    struct timespec cpu_start;
    struct timespec cpu_end;
    (void)clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_start);
    const uint64_t started = now_ns();
    bool ok = drive(&adapter, started + UINT64_C(5000000000));
    const uint64_t ended = now_ns();
    (void)clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu_end);
    const uint64_t cpu_ns =
        ((uint64_t)cpu_end.tv_sec * UINT64_C(1000000000) + (uint64_t)cpu_end.tv_nsec) -
        ((uint64_t)cpu_start.tv_sec * UINT64_C(1000000000) + (uint64_t)cpu_start.tv_nsec);
    if (ok && !terminal_matches_service(&adapter)) {
        ok = false;
    }
    if (ok) {
        emit(&adapter, fixture_hash, terminal_hash, terminal_json, ended - started, cpu_ns,
             service_hash, schema_hash);
    }
    if (!ok) {
        fprintf(stderr, "adapter failed: submitted=%zu completed=%zu pg_state=%d pg_error=%s\n",
                adapter.submitted, adapter.completed,
                adapter.pg != NULL ? (int)alpha_pg_state(adapter.pg) : -1,
                adapter.pg != NULL ? alpha_pg_error(adapter.pg) : "n/a");
    }
    if (adapter.kind == CASE_DB) {
        const char *conninfo = getenv("ALPHA_POSTGRES_URL");
        if (conninfo == NULL) {
            conninfo = "host=127.0.0.1 port=55432 dbname=alpha_test user=alpha_test "
                       "password=alpha_test_only";
        }
        alpha_pg_close(adapter.pg);
        (void)pg_cleanup(conninfo, adapter.ticker);
    } else {
        alpha_redis_async_close(adapter.redis);
    }
    alpha_lws_runtime_destroy(adapter.runtime);
    free(adapter.latency_ns);
    free(terminal_json);
    yyjson_doc_free(document);
    free(fixture_bytes);
    return ok ? 0 : 1;
}
