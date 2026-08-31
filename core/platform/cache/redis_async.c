#include "alpha/redis_async.h"

#include <stdlib.h>
#include <string.h>

#include <hiredis/async.h>

struct alpha_redis_async {
    redisAsyncContext *context;
    char *host;
    int port;
    alpha_redis_watch_fn watch_fn;
    alpha_redis_timer_fn timer_fn;
    alpha_redis_connect_fn connect_fn;
    alpha_redis_reply_fn reply_fn;
    void *callback_ctx;
    unsigned watch;
    size_t pending;
};

static void publish_watch(alpha_redis_async_t *redis) {
    const int socket_fd = redis->context != NULL ? redis->context->c.fd : -1;
    redis->watch_fn(redis->callback_ctx, socket_fd, (alpha_redis_watch_t)redis->watch);
}

static void add_read(void *data) {
    alpha_redis_async_t *redis = data;
    redis->watch |= (unsigned)ALPHA_REDIS_WATCH_READ;
    publish_watch(redis);
}
static void del_read(void *data) {
    alpha_redis_async_t *redis = data;
    redis->watch &= ~(unsigned)ALPHA_REDIS_WATCH_READ;
    publish_watch(redis);
}
static void add_write(void *data) {
    alpha_redis_async_t *redis = data;
    redis->watch |= (unsigned)ALPHA_REDIS_WATCH_WRITE;
    publish_watch(redis);
}
static void del_write(void *data) {
    alpha_redis_async_t *redis = data;
    redis->watch &= ~(unsigned)ALPHA_REDIS_WATCH_WRITE;
    publish_watch(redis);
}
static void cleanup(void *data) {
    alpha_redis_async_t *redis = data;
    redis->watch = ALPHA_REDIS_WATCH_NONE;
    publish_watch(redis);
}
static void schedule_timer(void *data, struct timeval interval) {
    alpha_redis_async_t *redis = data;
    redis->timer_fn(redis->callback_ctx, interval.tv_sec * 1000L + interval.tv_usec / 1000L);
}
static void connected(const redisAsyncContext *context, int status) {
    alpha_redis_async_t *redis = context->data;
    redis->connect_fn(redis->callback_ctx, status == REDIS_OK ? ALPHA_OK : ALPHA_ERR_IO);
}
static void disconnected(const redisAsyncContext *context, int status) {
    (void)status;
    alpha_redis_async_t *redis = context->data;
    redis->context = NULL;
    redis->watch = ALPHA_REDIS_WATCH_NONE;
    redis->watch_fn(redis->callback_ctx, context->c.fd, ALPHA_REDIS_WATCH_NONE);
    redis->connect_fn(redis->callback_ctx, ALPHA_ERR_IO);
}
static void replied(redisAsyncContext *context, void *raw, void *user_data) {
    alpha_redis_async_t *redis = context->data;
    redisReply *reply = raw;
    alpha_err_t status = ALPHA_ERR_IO;
    const char *text = NULL;
    long long integer = 0;
    if (redis->pending > 0) {
        redis->pending--;
    }
    if (reply != NULL && (reply->type == REDIS_REPLY_STRING || reply->type == REDIS_REPLY_STATUS)) {
        status = ALPHA_OK;
        text = reply->str;
    } else if (reply != NULL && reply->type == REDIS_REPLY_INTEGER) {
        status = ALPHA_OK;
        integer = reply->integer;
    } else if (reply != NULL && reply->type == REDIS_REPLY_NIL) {
        status = ALPHA_OK;
    }
    redis->reply_fn(redis->callback_ctx, status, text, integer, user_data);
}

static alpha_err_t start_connection(alpha_redis_async_t *redis) {
    redis->context = redisAsyncConnect(redis->host, redis->port);
    if (redis->context == NULL || redis->context->err != 0) {
        if (redis->context != NULL) {
            redisAsyncFree(redis->context);
            redis->context = NULL;
        }
        return ALPHA_ERR_IO;
    }
    redis->context->data = redis;
    redis->context->ev.data = redis;
    redis->context->ev.addRead = add_read;
    redis->context->ev.delRead = del_read;
    redis->context->ev.addWrite = add_write;
    redis->context->ev.delWrite = del_write;
    redis->context->ev.cleanup = cleanup;
    redis->context->ev.scheduleTimer = schedule_timer;
    if (redisAsyncSetConnectCallback(redis->context, connected) != REDIS_OK ||
        redisAsyncSetDisconnectCallback(redis->context, disconnected) != REDIS_OK) {
        redisAsyncFree(redis->context);
        redis->context = NULL;
        return ALPHA_ERR_IO;
    }
    return ALPHA_OK;
}

alpha_err_t alpha_redis_async_connect(const char *host, int port, alpha_redis_watch_fn watch_fn,
                                      alpha_redis_timer_fn timer_fn,
                                      alpha_redis_connect_fn connect_fn,
                                      alpha_redis_reply_fn reply_fn, void *callback_ctx,
                                      alpha_redis_async_t **out) {
    if (host == NULL || port <= 0 || watch_fn == NULL || timer_fn == NULL || connect_fn == NULL ||
        reply_fn == NULL || out == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    alpha_redis_async_t *redis = calloc(1, sizeof(*redis));
    if (redis == NULL) {
        return ALPHA_ERR_IO;
    }
    const size_t host_size = strlen(host) + 1;
    redis->host = malloc(host_size);
    if (redis->host == NULL) {
        free(redis);
        return ALPHA_ERR_IO;
    }
    memcpy(redis->host, host, host_size);
    redis->port = port;
    redis->watch_fn = watch_fn;
    redis->timer_fn = timer_fn;
    redis->connect_fn = connect_fn;
    redis->reply_fn = reply_fn;
    redis->callback_ctx = callback_ctx;
    if (start_connection(redis) != ALPHA_OK) {
        free(redis->host);
        free(redis);
        return ALPHA_ERR_IO;
    }
    *out = redis;
    return ALPHA_OK;
}

void alpha_redis_async_close(alpha_redis_async_t *redis) {
    if (redis == NULL) {
        return;
    }
    if (redis->context != NULL) {
        redisAsyncFree(redis->context);
    }
    free(redis->host);
    free(redis);
}
alpha_err_t alpha_redis_async_service(alpha_redis_async_t *redis, alpha_redis_watch_t ready) {
    if (redis == NULL || redis->context == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    if (((unsigned)ready & (unsigned)ALPHA_REDIS_WATCH_READ) != 0U) {
        redisAsyncHandleRead(redis->context);
    }
    if (redis->context != NULL && ((unsigned)ready & (unsigned)ALPHA_REDIS_WATCH_WRITE) != 0U) {
        redisAsyncHandleWrite(redis->context);
    }
    return redis->context != NULL && redis->context->err == 0 ? ALPHA_OK : ALPHA_ERR_IO;
}
alpha_err_t alpha_redis_async_timeout(alpha_redis_async_t *redis) {
    if (redis == NULL || redis->context == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    redisAsyncHandleTimeout(redis->context);
    return redis->context != NULL && redis->context->err == 0 ? ALPHA_OK : ALPHA_ERR_IO;
}
alpha_err_t alpha_redis_async_command(alpha_redis_async_t *redis, int argc, const char **argv,
                                      const size_t *lengths, void *user_data) {
    if (redis == NULL || redis->context == NULL || argc <= 0 || argv == NULL || lengths == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    if (redis->pending >= ALPHA_REDIS_ASYNC_MAX_PENDING) {
        return ALPHA_ERR_RANGE;
    }
    if (redisAsyncCommandArgv(redis->context, replied, user_data, argc, argv, lengths) !=
        REDIS_OK) {
        return ALPHA_ERR_IO;
    }
    redis->pending++;
    return ALPHA_OK;
}

static alpha_err_t latest_key(const char *ticker, char *key, size_t capacity, size_t *size_out) {
    static const char prefix[] = "redis:cache:latest_ticks:";
    if (ticker == NULL || ticker[0] == '\0' || key == NULL || size_out == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    const size_t ticker_size = strlen(ticker);
    if (ticker_size > capacity - sizeof(prefix)) {
        return ALPHA_ERR_RANGE;
    }
    memcpy(key, prefix, sizeof(prefix) - 1);
    memcpy(key + sizeof(prefix) - 1, ticker, ticker_size + 1);
    *size_out = sizeof(prefix) - 1 + ticker_size;
    return ALPHA_OK;
}

alpha_err_t alpha_redis_async_set_latest_tick(alpha_redis_async_t *redis, const char *ticker,
                                              const char *json_payload, void *user_data) {
    char key[256];
    size_t key_size = 0;
    const alpha_err_t key_status = latest_key(ticker, key, sizeof(key), &key_size);
    if (key_status != ALPHA_OK || json_payload == NULL) {
        return key_status != ALPHA_OK ? key_status : ALPHA_ERR_INVALID_ARG;
    }
    static const char ttl[] = "60";
    const char *args[] = {"SET", key, json_payload, "EX", ttl};
    const size_t lengths[] = {3, key_size, strlen(json_payload), 2, sizeof(ttl) - 1};
    return alpha_redis_async_command(redis, 5, args, lengths, user_data);
}

alpha_err_t alpha_redis_async_get_latest_tick(alpha_redis_async_t *redis, const char *ticker,
                                              void *user_data) {
    char key[256];
    size_t key_size = 0;
    const alpha_err_t status = latest_key(ticker, key, sizeof(key), &key_size);
    if (status != ALPHA_OK) {
        return status;
    }
    const char *args[] = {"GET", key};
    const size_t lengths[] = {3, key_size};
    return alpha_redis_async_command(redis, 2, args, lengths, user_data);
}

alpha_err_t alpha_redis_async_reconnect(alpha_redis_async_t *redis) {
    if (redis == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    if (redis->context != NULL) {
        return ALPHA_ERR_RANGE;
    }
    return start_connection(redis);
}

size_t alpha_redis_async_pending(const alpha_redis_async_t *redis) {
    return redis != NULL ? redis->pending : 0;
}
int alpha_redis_async_socket(const alpha_redis_async_t *redis) {
    return redis != NULL && redis->context != NULL ? redis->context->c.fd : -1;
}
