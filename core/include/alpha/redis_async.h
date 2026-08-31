#ifndef ALPHA_REDIS_ASYNC_H
#define ALPHA_REDIS_ASYNC_H

#include <stddef.h>

#include "alpha/errors.h"

typedef struct alpha_redis_async alpha_redis_async_t;

#define ALPHA_REDIS_ASYNC_MAX_PENDING 64U

typedef enum {
    ALPHA_REDIS_WATCH_NONE = 0,
    ALPHA_REDIS_WATCH_READ = 1,
    ALPHA_REDIS_WATCH_WRITE = 2,
    ALPHA_REDIS_WATCH_READ_WRITE = 3,
} alpha_redis_watch_t;

typedef void (*alpha_redis_watch_fn)(void *ctx, int socket_fd, alpha_redis_watch_t watch);
typedef void (*alpha_redis_timer_fn)(void *ctx, long timeout_ms);
typedef void (*alpha_redis_connect_fn)(void *ctx, alpha_err_t status);
typedef void (*alpha_redis_reply_fn)(void *ctx, alpha_err_t status, const char *text,
                                     long long integer, void *user_data);

/* Hiredis-async adapter. Callbacks register its fd/timer with the LWS owner;
 * this module never waits, polls, sleeps, or starts another event loop. */
alpha_err_t alpha_redis_async_connect(const char *host, int port, alpha_redis_watch_fn watch_fn,
                                      alpha_redis_timer_fn timer_fn,
                                      alpha_redis_connect_fn connect_fn,
                                      alpha_redis_reply_fn reply_fn, void *callback_ctx,
                                      alpha_redis_async_t **out);
void alpha_redis_async_close(alpha_redis_async_t *redis);
alpha_err_t alpha_redis_async_service(alpha_redis_async_t *redis, alpha_redis_watch_t ready);
alpha_err_t alpha_redis_async_timeout(alpha_redis_async_t *redis);
alpha_err_t alpha_redis_async_command(alpha_redis_async_t *redis, int argc, const char **argv,
                                      const size_t *lengths, void *user_data);
alpha_err_t alpha_redis_async_set_latest_tick(alpha_redis_async_t *redis, const char *ticker,
                                              const char *json_payload, void *user_data);
alpha_err_t alpha_redis_async_get_latest_tick(alpha_redis_async_t *redis, const char *ticker,
                                              void *user_data);
/* Recreate the hiredis context after a disconnect. Retry/backoff belongs to the owner loop. */
alpha_err_t alpha_redis_async_reconnect(alpha_redis_async_t *redis);
size_t alpha_redis_async_pending(const alpha_redis_async_t *redis);
int alpha_redis_async_socket(const alpha_redis_async_t *redis);

#endif
