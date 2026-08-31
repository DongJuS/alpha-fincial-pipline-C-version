#include "alpha/http_client.h"
#include "alpha/lws_runtime.h"
#include "alpha/postgres.h"
#include "alpha/redis_async.h"
#include "unity.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef enum { WATCH_PG, WATCH_REDIS, WATCH_HTTP } watch_owner_t;
typedef struct integration integration_t;

typedef struct {
    integration_t *integration;
    watch_owner_t owner;
    int fd;
    alpha_lws_watch_t *watch;
    bool active;
} driver_watch_t;

struct integration {
    alpha_lws_runtime_t *runtime;
    alpha_pg_t *pg;
    alpha_redis_async_t *redis;
    alpha_http_multi_t *http;
    driver_watch_t watches[16];
    bool pg_sent;
    bool pg_done;
    bool redis_connected;
    bool redis_done;
    bool http_done;
    alpha_err_t failure;
};

void setUp(void) {}
void tearDown(void) {}

static driver_watch_t *find_watch(integration_t *state, watch_owner_t owner, int fd) {
    for (size_t i = 0; i < sizeof(state->watches) / sizeof(state->watches[0]); ++i) {
        if (state->watches[i].active && state->watches[i].owner == owner &&
            state->watches[i].fd == fd) {
            return &state->watches[i];
        }
    }
    return NULL;
}

static void driver_ready(void *ctx, bool readable, bool writable, bool timed_out) {
    driver_watch_t *entry = ctx;
    integration_t *state = entry->integration;
    alpha_err_t status = ALPHA_OK;
    if (entry->owner == WATCH_PG) {
        status = alpha_pg_service(state->pg, readable, writable);
        if (status == ALPHA_OK) {
            status = alpha_lws_watch_set_write(entry->watch, alpha_pg_wants_write(state->pg));
        }
    } else if (entry->owner == WATCH_REDIS) {
        if (timed_out) {
            status = alpha_redis_async_timeout(state->redis);
        } else {
            alpha_redis_watch_t ready = ALPHA_REDIS_WATCH_NONE;
            if (readable && writable) {
                ready = ALPHA_REDIS_WATCH_READ_WRITE;
            } else if (readable) {
                ready = ALPHA_REDIS_WATCH_READ;
            } else if (writable) {
                ready = ALPHA_REDIS_WATCH_WRITE;
            }
            status = alpha_redis_async_service(state->redis, ready);
        }
    } else if (timed_out) {
        int running = 0;
        status = alpha_http_multi_timeout_action(state->http, &running);
    } else {
        alpha_http_watch_t ready = ALPHA_HTTP_WATCH_NONE;
        if (readable && writable) {
            ready = ALPHA_HTTP_WATCH_READ_WRITE;
        } else if (readable) {
            ready = ALPHA_HTTP_WATCH_READ;
        } else if (writable) {
            ready = ALPHA_HTTP_WATCH_WRITE;
        }
        int running = 0;
        status = alpha_http_multi_socket_action(state->http, entry->fd, ready, &running);
    }
    if (status != ALPHA_OK) {
        state->failure = status;
    }
}

static driver_watch_t *ensure_watch(integration_t *state, watch_owner_t owner, int fd,
                                    bool wants_write) {
    driver_watch_t *entry = find_watch(state, owner, fd);
    if (entry != NULL) {
        if (alpha_lws_watch_set_write(entry->watch, wants_write) != ALPHA_OK) {
            state->failure = ALPHA_ERR_IO;
        }
        return entry;
    }
    for (size_t i = 0; i < sizeof(state->watches) / sizeof(state->watches[0]); ++i) {
        if (!state->watches[i].active) {
            entry = &state->watches[i];
            *entry =
                (driver_watch_t){.integration = state, .owner = owner, .fd = fd, .active = true};
            if (alpha_lws_watch_add(state->runtime, fd, wants_write, driver_ready, entry,
                                    &entry->watch) != ALPHA_OK) {
                entry->active = false;
                state->failure = ALPHA_ERR_IO;
                return NULL;
            }
            return entry;
        }
    }
    state->failure = ALPHA_ERR_RANGE;
    return NULL;
}

static void redis_watch_changed(void *ctx, int fd, alpha_redis_watch_t watch) {
    integration_t *state = ctx;
    (void)ensure_watch(state, WATCH_REDIS, fd,
                       watch == ALPHA_REDIS_WATCH_WRITE || watch == ALPHA_REDIS_WATCH_READ_WRITE);
}

static void redis_timer_changed(void *ctx, long timeout_ms) {
    integration_t *state = ctx;
    driver_watch_t *entry = find_watch(state, WATCH_REDIS, alpha_redis_async_socket(state->redis));
    if (entry != NULL && alpha_lws_watch_set_timer(entry->watch, timeout_ms) != ALPHA_OK) {
        state->failure = ALPHA_ERR_IO;
    }
}

static void redis_connected(void *ctx, alpha_err_t status) {
    integration_t *state = ctx;
    state->redis_connected = status == ALPHA_OK;
    if (status != ALPHA_OK) {
        state->failure = status;
        return;
    }
    const char *argv[] = {"PING"};
    const size_t lengths[] = {4};
    if (alpha_redis_async_command(state->redis, 1, argv, lengths, NULL) != ALPHA_OK) {
        state->failure = ALPHA_ERR_IO;
    }
}

static void redis_replied(void *ctx, alpha_err_t status, const char *text, long long integer,
                          void *user_data) {
    integration_t *state = ctx;
    (void)integer;
    (void)user_data;
    state->redis_done = status == ALPHA_OK && text != NULL && strcmp(text, "PONG") == 0;
    if (!state->redis_done) {
        state->failure = ALPHA_ERR_IO;
    }
}

static void http_watch_changed(void *ctx, int fd, alpha_http_watch_t watch) {
    integration_t *state = ctx;
    driver_watch_t *entry = find_watch(state, WATCH_HTTP, fd);
    if (watch == ALPHA_HTTP_WATCH_NONE) {
        if (entry != NULL) {
            alpha_lws_watch_remove(entry->watch);
            entry->active = false;
        }
        return;
    }
    (void)ensure_watch(state, WATCH_HTTP, fd,
                       watch == ALPHA_HTTP_WATCH_WRITE || watch == ALPHA_HTTP_WATCH_READ_WRITE);
}

static void http_timer_changed(void *ctx, long timeout_ms) {
    integration_t *state = ctx;
    for (size_t i = 0; i < sizeof(state->watches) / sizeof(state->watches[0]); ++i) {
        if (state->watches[i].active && state->watches[i].owner == WATCH_HTTP) {
            if (alpha_lws_watch_set_timer(state->watches[i].watch, timeout_ms) != ALPHA_OK) {
                state->failure = ALPHA_ERR_IO;
            }
            return;
        }
    }
}

static void http_completed(void *ctx, const alpha_http_result_t *result) {
    integration_t *state = ctx;
    state->http_done = result->transport_code == 0 && result->status_code == 200;
    if (!state->http_done) {
        state->failure = ALPHA_ERR_HTTP;
    }
}

static void pg_completed(const alpha_pg_result_t *result, void *user_data) {
    integration_t *state = user_data;
    state->pg_done = result->status == ALPHA_OK && result->kind == ALPHA_PG_RESULT_EXPOSURE;
    if (!state->pg_done) {
        state->failure = ALPHA_ERR_DB;
    }
}

static int start_http_server(uint16_t *port_out) {
    const int server = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address = {
        .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK), .sin_port = 0};
    socklen_t length = sizeof(address);
    if (server < 0 || bind(server, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        getsockname(server, (struct sockaddr *)&address, &length) != 0 || listen(server, 1) != 0) {
        if (server >= 0) {
            close(server);
        }
        return -1;
    }
    *port_out = ntohs(address.sin_port);
    return server;
}

static void serve_http_once(int server) {
    const int client = accept(server, NULL, NULL);
    char request[1024];
    if (client < 0 || read(client, request, sizeof(request)) <= 0) {
        _exit(2);
    }
    static const char response[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";
    const ssize_t ignored = write(client, response, sizeof(response) - 1);
    (void)ignored;
    close(client);
    close(server);
    _exit(0);
}

static void test_three_drivers_share_one_lws_service_loop(void) {
    const char *required = getenv("ALPHA_RUN_LWS_DRIVER_INTEGRATION");
    integration_t state = {0};
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_lws_runtime_create(16, &state.runtime));

    const char *conninfo = getenv("ALPHA_POSTGRES_URL");
    if (conninfo == NULL) {
        conninfo = "host=127.0.0.1 port=55432 dbname=alpha_test user=alpha_test "
                   "password=alpha_test_only";
    }
    if (alpha_pg_connect_start(conninfo, 2, &state.pg) != ALPHA_OK) {
        if (required == NULL) {
            alpha_lws_runtime_destroy(state.runtime);
            TEST_IGNORE_MESSAGE("shared services unavailable");
        }
        TEST_FAIL_MESSAGE("required PostgreSQL unavailable");
    }
    driver_watch_t *pg_watch = ensure_watch(&state, WATCH_PG, alpha_pg_socket(state.pg), true);
    TEST_ASSERT_NOT_NULL(pg_watch);

    TEST_ASSERT_EQUAL_INT(ALPHA_OK,
                          alpha_redis_async_connect("127.0.0.1", 56379, redis_watch_changed,
                                                    redis_timer_changed, redis_connected,
                                                    redis_replied, &state, &state.redis));
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_http_global_init());
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_http_multi_create(http_watch_changed, http_timer_changed,
                                                            http_completed, &state, &state.http));

    uint16_t port = 0;
    const int server = start_http_server(&port);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, server);
    const pid_t child = fork();
    TEST_ASSERT_NOT_EQUAL(-1, child);
    if (child == 0) {
        serve_http_once(server);
    }
    close(server);
    char url[128];
    (void)snprintf(url, sizeof(url), "http://127.0.0.1:%u/replay", (unsigned)port);
    const alpha_http_post_t post = {
        .kind = ALPHA_HTTP_POST_GENERIC_JSON, .url = url, .json_body = "{}", .timeout_ms = 2000};
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_http_post_json(state.http, &post));
    int running = 0;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_http_multi_timeout_action(state.http, &running));

    for (int i = 0; i < 1000 && state.failure == ALPHA_OK &&
                    !(state.pg_done && state.redis_done && state.http_done);
         ++i) {
        TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_lws_runtime_service(state.runtime, 10));
        if (!state.pg_sent && alpha_pg_state(state.pg) == ALPHA_PG_READY) {
            TEST_ASSERT_EQUAL_INT(
                ALPHA_OK, alpha_pg_query_exposure(state.pg, "005930", pg_completed, &state));
            state.pg_sent = true;
            TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_lws_watch_set_write(pg_watch->watch, true));
        }
    }
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, state.failure);
    TEST_ASSERT_TRUE(state.pg_done);
    TEST_ASSERT_TRUE(state.redis_connected);
    TEST_ASSERT_TRUE(state.redis_done);
    TEST_ASSERT_TRUE(state.http_done);

    int child_status = 0;
    TEST_ASSERT_EQUAL_INT(child, waitpid(child, &child_status, 0));
    TEST_ASSERT_TRUE(WIFEXITED(child_status));
    TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(child_status));
    alpha_http_multi_destroy(state.http);
    alpha_http_global_cleanup();
    alpha_redis_async_close(state.redis);
    alpha_lws_watch_remove(pg_watch->watch);
    alpha_pg_close(state.pg);
    alpha_lws_runtime_destroy(state.runtime);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_three_drivers_share_one_lws_service_loop);
    return UNITY_END();
}
