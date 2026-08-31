#ifndef ALPHA_LWS_RUNTIME_H
#define ALPHA_LWS_RUNTIME_H

#include "alpha/errors.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct alpha_lws_runtime alpha_lws_runtime_t;
typedef struct alpha_lws_watch alpha_lws_watch_t;

typedef void (*alpha_lws_ready_fn)(void *ctx, bool readable, bool writable, bool timed_out);

/* Creates one non-listening libwebsockets context with a bounded foreign-fd
 * registry. The caller that invokes service() is the sole event-loop owner. */
alpha_err_t alpha_lws_runtime_create(size_t watch_capacity, alpha_lws_runtime_t **out);
void alpha_lws_runtime_destroy(alpha_lws_runtime_t *runtime);

/* LWS adopts a duplicate of fd: the driver retains ownership of its original
 * descriptor while LWS owns and closes only the duplicate. */
alpha_err_t alpha_lws_watch_add(alpha_lws_runtime_t *runtime, int fd, bool wants_write,
                                alpha_lws_ready_fn ready_fn, void *callback_ctx,
                                alpha_lws_watch_t **out);
alpha_err_t alpha_lws_watch_set_write(alpha_lws_watch_t *watch, bool wants_write);
alpha_err_t alpha_lws_watch_set_timer(alpha_lws_watch_t *watch, int64_t timeout_ms);
void alpha_lws_watch_remove(alpha_lws_watch_t *watch);
int alpha_lws_watch_source_fd(const alpha_lws_watch_t *watch);

alpha_err_t alpha_lws_runtime_service(alpha_lws_runtime_t *runtime, int timeout_ms);
size_t alpha_lws_runtime_watch_count(const alpha_lws_runtime_t *runtime);
size_t alpha_lws_runtime_watch_capacity(const alpha_lws_runtime_t *runtime);

#endif
