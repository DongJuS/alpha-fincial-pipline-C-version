#include "alpha/redis_async.h"

#include <stdlib.h>

#include <hiredis/async.h>

struct alpha_redis_async {
    redisAsyncContext *context;
    alpha_redis_watch_fn watch_fn;
    alpha_redis_timer_fn timer_fn;
    alpha_redis_connect_fn connect_fn;
    alpha_redis_reply_fn reply_fn;
    void *callback_ctx;
    unsigned watch;
};

static void publish_watch(alpha_redis_async_t *redis) {
    redis->watch_fn(redis->callback_ctx, redis->context->c.fd, (alpha_redis_watch_t)redis->watch);
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
static void replied(redisAsyncContext *context, void *raw, void *user_data) {
    alpha_redis_async_t *redis = context->data;
    redisReply *reply = raw;
    if (reply != NULL && (reply->type == REDIS_REPLY_STRING || reply->type == REDIS_REPLY_STATUS)) {
        redis->reply_fn(redis->callback_ctx, ALPHA_OK, reply->str, 0, user_data);
    } else if (reply != NULL && reply->type == REDIS_REPLY_INTEGER) {
        redis->reply_fn(redis->callback_ctx, ALPHA_OK, NULL, reply->integer, user_data);
    } else if (reply != NULL && reply->type == REDIS_REPLY_NIL) {
        redis->reply_fn(redis->callback_ctx, ALPHA_OK, NULL, 0, user_data);
    } else {
        redis->reply_fn(redis->callback_ctx, ALPHA_ERR_IO, NULL, 0, user_data);
    }
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
    redis->context = redisAsyncConnect(host, port);
    if (redis->context == NULL || redis->context->err != 0) {
        if (redis->context != NULL) {
            redisAsyncFree(redis->context);
        }
        free(redis);
        return ALPHA_ERR_IO;
    }
    redis->watch_fn = watch_fn;
    redis->timer_fn = timer_fn;
    redis->connect_fn = connect_fn;
    redis->reply_fn = reply_fn;
    redis->callback_ctx = callback_ctx;
    redis->context->data = redis;
    redis->context->ev.data = redis;
    redis->context->ev.addRead = add_read;
    redis->context->ev.delRead = del_read;
    redis->context->ev.addWrite = add_write;
    redis->context->ev.delWrite = del_write;
    redis->context->ev.cleanup = cleanup;
    redis->context->ev.scheduleTimer = schedule_timer;
    if (redisAsyncSetConnectCallback(redis->context, connected) != REDIS_OK) {
        redisAsyncFree(redis->context);
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
    free(redis);
}
alpha_err_t alpha_redis_async_service(alpha_redis_async_t *redis, alpha_redis_watch_t ready) {
    if (redis == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    if (((unsigned)ready & (unsigned)ALPHA_REDIS_WATCH_READ) != 0U) {
        redisAsyncHandleRead(redis->context);
    }
    if (((unsigned)ready & (unsigned)ALPHA_REDIS_WATCH_WRITE) != 0U) {
        redisAsyncHandleWrite(redis->context);
    }
    return redis->context->err == 0 ? ALPHA_OK : ALPHA_ERR_IO;
}
alpha_err_t alpha_redis_async_timeout(alpha_redis_async_t *redis) {
    if (redis == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    redisAsyncHandleTimeout(redis->context);
    return redis->context->err == 0 ? ALPHA_OK : ALPHA_ERR_IO;
}
alpha_err_t alpha_redis_async_command(alpha_redis_async_t *redis, int argc, const char **argv,
                                      const size_t *lengths, void *user_data) {
    if (redis == NULL || argc <= 0 || argv == NULL || lengths == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    return redisAsyncCommandArgv(redis->context, replied, user_data, argc, argv, lengths) ==
                   REDIS_OK
               ? ALPHA_OK
               : ALPHA_ERR_IO;
}
int alpha_redis_async_socket(const alpha_redis_async_t *redis) {
    return redis != NULL ? redis->context->c.fd : -1;
}
