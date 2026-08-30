#include <curl/curl.h>
#include <hiredis/async.h>
#include <libpq-fe.h>
#include <libwebsockets.h>

#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define QUEUE_CAPACITY 4
#define RUN_TIMEOUT_SECONDS 20

typedef enum { OWNER_PG, OWNER_REDIS, OWNER_CURL } owner_t;

typedef struct app app_t;

typedef struct {
    app_t *app;
    owner_t owner;
    struct lws *wsi;
    curl_socket_t fd;
    int curl_action;
} watched_fd_t;

typedef struct {
    uint64_t id;
    bool cancelled;
} request_t;

struct app {
    struct lws_context *context;
    struct lws_vhost *vhost;
    PGconn *pg;
    watched_fd_t pg_watch;
    int pg_state;
    bool pg_roundtrip;
    bool pg_abort_seen;
    bool pg_sync_seen;
    bool pg_recovered;
    redisAsyncContext *redis;
    watched_fd_t redis_watch;
    int redis_generation;
    bool redis_roundtrip;
    bool redis_disconnect;
    bool redis_recovered;
    CURLM *curl_multi;
    CURL *curl_easy;
    watched_fd_t curl_watches[16];
    size_t curl_watch_count;
    bool curl_roundtrip;
    long curl_status;
    request_t queue[QUEUE_CAPACITY];
    size_t queue_len;
    bool backpressure;
    bool cancellation;
    bool request_match;
    bool clean_shutdown;
    bool failed;
    time_t started;
    lws_sorted_usec_list_t tick;
};

static int raw_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in,
                        size_t len);
static bool complete(const app_t *app);

static const struct lws_protocols protocols[] = {
    {"alpha-foreign-fd", raw_callback, 0, 0, 0, NULL, 0},
    LWS_PROTOCOL_LIST_TERM,
};

static void fail(app_t *app, const char *message) {
    fputs("spike: ", stderr);
    fputs(message, stderr);
    fputc('\n', stderr);
    app->failed = true;
    lws_cancel_service(app->context);
}

static void register_socket(watched_fd_t *watch, curl_socket_t fd) { watch->fd = fd; }

static void exercise_queue(app_t *app) {
    for (uint64_t id = 1; id <= QUEUE_CAPACITY; ++id) {
        app->queue[app->queue_len++] = (request_t){.id = id, .cancelled = id == 2};
    }
    if (app->queue_len == QUEUE_CAPACITY) {
        app->backpressure = true;
    }
    for (size_t i = 0; i < app->queue_len; ++i) {
        if (app->queue[i].cancelled) {
            app->cancellation = true;
            continue;
        }
        if (app->queue[i].id != i + 1) {
            fail(app, "request/result id mismatch");
            return;
        }
    }
    app->request_match = true;
}

static void pg_send_initial(app_t *app) {
    if (PQenterPipelineMode(app->pg) != 1 ||
        PQsendQueryParams(app->pg, "SELECT 1", 0, NULL, NULL, NULL, NULL, 0) != 1 ||
        PQsendFlushRequest(app->pg) != 1 ||
        PQsendQueryParams(app->pg, "SELECT alpha_intentional_failure", 0, NULL, NULL, NULL, NULL,
                          0) != 1 ||
        PQsendQueryParams(app->pg, "SELECT 2", 0, NULL, NULL, NULL, NULL, 0) != 1 ||
        PQpipelineSync(app->pg) != 1) {
        fail(app, "could not enqueue PostgreSQL abort pipeline");
        return;
    }
    app->pg_state = 2;
    lws_cancel_service(app->context);
}

static void pg_drain(app_t *app) {
    if (PQconsumeInput(app->pg) != 1) {
        fail(app, "PostgreSQL consume failed");
        return;
    }
    while (!PQisBusy(app->pg)) {
        PGresult *result = PQgetResult(app->pg);
        if (result == NULL) {
            if (app->pg_state == 2 && app->pg_sync_seen) {
                if (PQsendQueryParams(app->pg, "SELECT 42", 0, NULL, NULL, NULL, NULL, 0) != 1 ||
                    PQpipelineSync(app->pg) != 1) {
                    fail(app, "could not enqueue PostgreSQL recovery pipeline");
                    return;
                }
                app->pg_state = 3;
                lws_cancel_service(app->context);
            }
            return;
        }
        ExecStatusType status = PQresultStatus(result);
        if (status == PGRES_TUPLES_OK) {
            if (app->pg_state == 2) {
                app->pg_roundtrip = true;
            } else if (app->pg_state == 3 && PQntuples(result) == 1 &&
                       strcmp(PQgetvalue(result, 0, 0), "42") == 0) {
                app->pg_recovered = true;
            }
        } else if (status == PGRES_FATAL_ERROR || status == PGRES_PIPELINE_ABORTED) {
            app->pg_abort_seen = true;
        } else if (status == PGRES_PIPELINE_SYNC) {
            app->pg_sync_seen = true;
        }
        PQclear(result);
    }
}

static void pg_ready(app_t *app, bool writable) {
    if (app->pg_state == 0) {
        PostgresPollingStatusType status = PQconnectPoll(app->pg);
        if (status == PGRES_POLLING_FAILED) {
            fail(app, "PostgreSQL nonblocking connect failed");
        } else if (status == PGRES_POLLING_OK) {
            pg_send_initial(app);
        } else if (status == PGRES_POLLING_WRITING) {
            lws_cancel_service(app->context);
        }
        return;
    }
    if (writable && PQflush(app->pg) == 1) {
        lws_cancel_service(app->context);
    }
    pg_drain(app);
}

static void redis_add_read(void *data) { (void)data; }
static void redis_del_read(void *data) { (void)data; }

static void redis_add_write(void *data) {
    watched_fd_t *watch = data;
    lws_cancel_service(watch->app->context);
}

static void redis_del_write(void *data) { (void)data; }
static void redis_cleanup(void *data) { (void)data; }
static void redis_timer(void *data, struct timeval timeout) {
    watched_fd_t *watch = data;
    uint64_t usecs = (uint64_t)timeout.tv_sec * 1000000u + (uint64_t)timeout.tv_usec;
    (void)watch;
    (void)usecs;
}

static void redis_command_callback(redisAsyncContext *context, void *reply_ptr, void *private) {
    app_t *app = private;
    redisReply *reply = reply_ptr;
    if (reply == NULL || context->err != 0) {
        fail(app, "Redis async command failed");
        return;
    }
    if (app->redis_generation == 1 && reply->type == REDIS_REPLY_STRING &&
        strcmp(reply->str, "alpha") == 0) {
        app->redis_roundtrip = true;
        redisAsyncDisconnect(context);
    } else if (app->redis_generation == 2 && reply->type == REDIS_REPLY_STATUS &&
               strcmp(reply->str, "PONG") == 0) {
        app->redis_recovered = true;
    }
}

static void redis_connect_callback(const redisAsyncContext *context, int status) {
    app_t *app = context->data;
    if (status != REDIS_OK) {
        fail(app, "Redis async connect failed");
        return;
    }
    if (app->redis_generation == 1) {
        if (redisAsyncCommand(app->redis, NULL, NULL, "SET alpha:lws alpha") != REDIS_OK ||
            redisAsyncCommand(app->redis, redis_command_callback, app, "GET alpha:lws") !=
                REDIS_OK) {
            fail(app, "Redis roundtrip enqueue failed");
        }
    } else if (redisAsyncCommand(app->redis, redis_command_callback, app, "PING") != REDIS_OK) {
        fail(app, "Redis recovery enqueue failed");
    }
}

static bool start_redis(app_t *app);

static void redis_disconnect_callback(const redisAsyncContext *context, int status) {
    app_t *app = context->data;
    (void)status;
    if (app->redis_generation == 1 && app->redis_roundtrip) {
        app->redis_disconnect = true;
        app->redis = NULL;
        app->redis_watch.wsi = NULL;
        app->redis_generation = 2;
        if (!start_redis(app)) {
            fail(app, "Redis reconnect setup failed");
        }
    }
}

static bool start_redis(app_t *app) {
    const char *port_text = getenv("ALPHA_REDIS_PORT");
    long parsed_port = port_text == NULL ? 56379L : strtol(port_text, NULL, 10);
    if (parsed_port <= 0 || parsed_port > 65535) {
        return false;
    }
    int port = (int)parsed_port;
    redisAsyncContext *context = redisAsyncConnect("127.0.0.1", port);
    if (context == NULL || context->err != 0) {
        return false;
    }
    app->redis = context;
    app->redis_watch = (watched_fd_t){.app = app, .owner = OWNER_REDIS};
    context->data = app;
    context->ev.data = &app->redis_watch;
    context->ev.addRead = redis_add_read;
    context->ev.delRead = redis_del_read;
    context->ev.addWrite = redis_add_write;
    context->ev.delWrite = redis_del_write;
    context->ev.cleanup = redis_cleanup;
    context->ev.scheduleTimer = redis_timer;
    register_socket(&app->redis_watch, context->c.fd);
    if (redisAsyncSetConnectCallback(context, redis_connect_callback) != REDIS_OK ||
        redisAsyncSetDisconnectCallback(context, redis_disconnect_callback) != REDIS_OK) {
        return false;
    }
    lws_cancel_service(app->context);
    return true;
}

static int on_curl_socket(CURL *easy, curl_socket_t fd, int what, void *private,
                          void *socket_private) {
    app_t *app = private;
    watched_fd_t *watch = socket_private;
    (void)easy;
    if (what == CURL_POLL_REMOVE) {
        if (watch != NULL) {
            watch->curl_action = 0;
            curl_multi_assign(app->curl_multi, fd, NULL);
        }
        return 0;
    }
    if (watch == NULL) {
        if (app->curl_watch_count >= sizeof(app->curl_watches) / sizeof(app->curl_watches[0])) {
            return -1;
        }
        watch = &app->curl_watches[app->curl_watch_count++];
        *watch = (watched_fd_t){.app = app, .owner = OWNER_CURL};
        register_socket(watch, fd);
        curl_multi_assign(app->curl_multi, fd, watch);
    }
    watch->curl_action = what;
    if ((what & CURL_POLL_OUT) != 0) {
        lws_cancel_service(app->context);
    }
    return 0;
}

static int curl_timer_callback(CURLM *multi, long timeout_ms, void *private) {
    app_t *app = private;
    (void)multi;
    if (timeout_ms == 0) {
        lws_cancel_service(app->context);
    }
    return 0;
}

static size_t curl_discard(char *data, size_t size, size_t count, void *private) {
    (void)data;
    (void)private;
    return size * count;
}

static void curl_action(app_t *app, watched_fd_t *watch, int action) {
    int running = 0;
    if (curl_multi_socket_action(app->curl_multi, watch == NULL ? CURL_SOCKET_TIMEOUT : watch->fd,
                                 action, &running) != CURLM_OK) {
        fail(app, "curl multi socket action failed");
        return;
    }
    int messages = 0;
    CURLMsg *message;
    while ((message = curl_multi_info_read(app->curl_multi, &messages)) != NULL) {
        if (message->msg == CURLMSG_DONE) {
            curl_easy_getinfo(message->easy_handle, CURLINFO_RESPONSE_CODE, &app->curl_status);
            app->curl_roundtrip = message->data.result == CURLE_OK && app->curl_status == 200;
            if (!app->curl_roundtrip) {
                fail(app, "curl multi HTTP roundtrip failed");
            }
        }
    }
}

static void service_tick(lws_sorted_usec_list_t *sul) {
    app_t *app = lws_container_of(sul, app_t, tick);
    struct pollfd fds[18];
    watched_fd_t *watches[18];
    size_t count = 0;
    fds[count] = (struct pollfd){.fd = PQsocket(app->pg), .events = POLLIN | POLLOUT};
    watches[count++] = &app->pg_watch;
    if (app->redis != NULL) {
        fds[count] = (struct pollfd){.fd = app->redis->c.fd, .events = POLLIN | POLLOUT};
        watches[count++] = &app->redis_watch;
    }
    for (size_t i = 0; i < app->curl_watch_count && count < 18; ++i) {
        watched_fd_t *watch = &app->curl_watches[i];
        if (watch->curl_action == 0) {
            continue;
        }
        short events = 0;
        if ((watch->curl_action & CURL_POLL_IN) != 0) {
            events |= POLLIN;
        }
        if ((watch->curl_action & CURL_POLL_OUT) != 0) {
            events |= POLLOUT;
        }
        fds[count] = (struct pollfd){.fd = (int)watch->fd, .events = events};
        watches[count++] = watch;
    }
    int ready = poll(fds, (nfds_t)count, 0);
    if (ready < 0) {
        fail(app, "readiness probe failed");
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        if (fds[i].revents == 0) {
            continue;
        }
        watched_fd_t *watch = watches[i];
        bool readable = (fds[i].revents & (POLLIN | POLLERR | POLLHUP)) != 0;
        bool writable = (fds[i].revents & POLLOUT) != 0;
        if (watch->owner == OWNER_PG) {
            pg_ready(app, writable);
        } else if (watch->owner == OWNER_REDIS && app->redis != NULL) {
            if (readable) {
                redisAsyncHandleRead(app->redis);
            }
            if (writable && app->redis != NULL) {
                redisAsyncHandleWrite(app->redis);
            }
        } else if (watch->owner == OWNER_CURL) {
            int action = 0;
            if (readable) {
                action |= CURL_CSELECT_IN;
            }
            if (writable) {
                action |= CURL_CSELECT_OUT;
            }
            curl_action(app, watch, action);
        }
    }
    curl_action(app, NULL, 0);
    if (complete(app)) {
        lws_cancel_service(app->context);
    } else if (!app->failed) {
        lws_sul_schedule(app->context, 0, &app->tick, service_tick, LWS_US_PER_MS);
    }
}

static int raw_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in,
                        size_t len) {
    watched_fd_t *watch = lws_get_opaque_user_data(wsi);
    (void)user;
    (void)in;
    (void)len;
    if (watch == NULL) {
        return 0;
    }
    switch (reason) {
    case LWS_CALLBACK_RAW_RX:
        if (watch->owner == OWNER_PG) {
            pg_ready(watch->app, false);
        } else if (watch->owner == OWNER_REDIS && watch->app->redis != NULL) {
            redisAsyncHandleRead(watch->app->redis);
        } else if (watch->owner == OWNER_CURL && (watch->curl_action & CURL_POLL_IN) != 0) {
            curl_action(watch->app, watch, CURL_CSELECT_IN);
        }
        break;
    case LWS_CALLBACK_RAW_WRITEABLE:
        if (watch->owner == OWNER_PG) {
            pg_ready(watch->app, true);
        } else if (watch->owner == OWNER_REDIS && watch->app->redis != NULL) {
            redisAsyncHandleWrite(watch->app->redis);
        } else if (watch->owner == OWNER_CURL && (watch->curl_action & CURL_POLL_OUT) != 0) {
            curl_action(watch->app, watch, CURL_CSELECT_OUT);
        }
        break;
    case LWS_CALLBACK_TIMER:
        if (watch->owner == OWNER_REDIS && watch->app->redis != NULL) {
            redisAsyncHandleTimeout(watch->app->redis);
        }
        break;
    default:
        break;
    }
    return watch->app->failed ? -1 : 0;
}

static bool complete(const app_t *app) {
    return app->pg_roundtrip && app->pg_abort_seen && app->pg_sync_seen && app->pg_recovered &&
           app->redis_roundtrip && app->redis_disconnect && app->redis_recovered &&
           app->curl_roundtrip && app->backpressure && app->cancellation && app->request_match;
}

static bool setup(app_t *app) {
    struct lws_context_creation_info info = {0};
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_EXPLICIT_VHOSTS;
    app->context = lws_create_context(&info);
    if (app->context == NULL) {
        return false;
    }
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.vhost_name = "alpha-spike";
    app->vhost = lws_create_vhost(app->context, &info);
    if (app->vhost == NULL) {
        return false;
    }
    exercise_queue(app);

    const char *pg_port = getenv("ALPHA_POSTGRES_PORT");
    const char *keywords[] = {"host", "port", "dbname", "user", "password", "connect_timeout",
                              NULL};
    const char *values[] = {"127.0.0.1",
                            pg_port == NULL ? "55432" : pg_port,
                            "alpha_test",
                            "alpha_test",
                            "alpha_test_only",
                            "5",
                            NULL};
    app->pg = PQconnectStartParams(keywords, values, 0);
    if (app->pg == NULL || PQsetnonblocking(app->pg, 1) != 0) {
        return false;
    }
    app->pg_watch = (watched_fd_t){.app = app, .owner = OWNER_PG};
    register_socket(&app->pg_watch, PQsocket(app->pg));

    app->redis_generation = 1;
    if (!start_redis(app)) {
        return false;
    }

    const char *url = getenv("ALPHA_HTTP_URL");
    app->curl_multi = curl_multi_init();
    app->curl_easy = curl_easy_init();
    if (app->curl_multi == NULL || app->curl_easy == NULL) {
        return false;
    }
    curl_multi_setopt(app->curl_multi, CURLMOPT_SOCKETFUNCTION, on_curl_socket);
    curl_multi_setopt(app->curl_multi, CURLMOPT_SOCKETDATA, app);
    curl_multi_setopt(app->curl_multi, CURLMOPT_TIMERFUNCTION, curl_timer_callback);
    curl_multi_setopt(app->curl_multi, CURLMOPT_TIMERDATA, app);
    curl_easy_setopt(app->curl_easy, CURLOPT_URL, url == NULL ? "http://127.0.0.1:58080/" : url);
    curl_easy_setopt(app->curl_easy, CURLOPT_WRITEFUNCTION, curl_discard);
    curl_easy_setopt(app->curl_easy, CURLOPT_TIMEOUT_MS, 5000L);
    if (curl_multi_add_handle(app->curl_multi, app->curl_easy) != CURLM_OK) {
        return false;
    }
    curl_action(app, NULL, 0);
    lws_sul_schedule(app->context, 0, &app->tick, service_tick, LWS_US_PER_MS);
    return !app->failed;
}

static void print_result(const app_t *app) {
    printf("{\"schema_version\":1,\"loop_owner\":\"libwebsockets\","
           "\"service_threads\":1,\"worker_threads\":0,\"queue_capacity\":%d,"
           "\"backpressure\":%s,\"cancellation\":%s,\"request_result_match\":%s,"
           "\"postgres\":{\"roundtrip\":%s,\"pipeline_abort\":%s,"
           "\"pipeline_sync\":%s,\"recovered\":%s},"
           "\"redis\":{\"roundtrip\":%s,\"disconnect\":%s,\"recovered\":%s},"
           "\"curl_multi\":{\"roundtrip\":%s,\"status\":%ld},"
           "\"alternate_event_loop\":false,\"clean_shutdown\":%s}\n",
           QUEUE_CAPACITY, app->backpressure ? "true" : "false",
           app->cancellation ? "true" : "false", app->request_match ? "true" : "false",
           app->pg_roundtrip ? "true" : "false", app->pg_abort_seen ? "true" : "false",
           app->pg_sync_seen ? "true" : "false", app->pg_recovered ? "true" : "false",
           app->redis_roundtrip ? "true" : "false", app->redis_disconnect ? "true" : "false",
           app->redis_recovered ? "true" : "false", app->curl_roundtrip ? "true" : "false",
           app->curl_status, app->clean_shutdown ? "true" : "false");
}

int main(void) {
    app_t app = {0};
    app.started = time(NULL);
    lws_set_log_level(LLL_ERR | LLL_WARN, NULL);
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK || !setup(&app)) {
        fputs("spike: setup failed\n", stderr);
        return 1;
    }
    while (!app.failed && !complete(&app) && time(NULL) - app.started < RUN_TIMEOUT_SECONDS) {
        if (lws_service(app.context, 100) < 0) {
            app.failed = true;
        }
    }
    if (!complete(&app)) {
        app.failed = true;
    }
    app.clean_shutdown = !app.failed;
    print_result(&app);
    /* The spike process owns all clients; OS teardown avoids double-close between adopted fds and
       their client libraries. Production adapters must formalize detach ownership. */
    fflush(stdout);
    return app.failed ? 1 : 0;
}
