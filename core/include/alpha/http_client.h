#ifndef ALPHA_HTTP_CLIENT_H
#define ALPHA_HTTP_CLIENT_H

#include "alpha/errors.h"

#include <stddef.h>

typedef struct alpha_http_multi alpha_http_multi_t;

#define ALPHA_HTTP_MAX_PENDING 64U
#define ALPHA_HTTP_MAX_RESPONSE_BYTES (1024U * 1024U)

typedef enum {
    ALPHA_HTTP_POST_GENERIC_JSON = 0,
    ALPHA_HTTP_POST_TELEGRAM,
    ALPHA_HTTP_POST_LLM,
} alpha_http_post_kind_t;

typedef enum {
    ALPHA_HTTP_WATCH_NONE = 0,
    ALPHA_HTTP_WATCH_READ = 1,
    ALPHA_HTTP_WATCH_WRITE = 2,
    ALPHA_HTTP_WATCH_READ_WRITE = 3,
} alpha_http_watch_t;

typedef struct {
    alpha_http_post_kind_t kind;
    const char *url;
    const char *json_body;
    /* Optional complete header line, for example "Authorization: Bearer ...". */
    const char *authorization_header;
    long timeout_ms;
    void *user_data;
} alpha_http_post_t;

typedef struct {
    alpha_http_post_kind_t kind;
    long status_code;
    int transport_code;
    const char *response_body;
    size_t response_size;
    void *user_data;
} alpha_http_result_t;

typedef void (*alpha_http_watch_fn)(void *ctx, int socket_fd, alpha_http_watch_t watch);
typedef void (*alpha_http_timer_fn)(void *ctx, long timeout_ms);
typedef void (*alpha_http_complete_fn)(void *ctx, const alpha_http_result_t *result);

/* Call once for the process, before threads; cleanup after all clients are destroyed. */
alpha_err_t alpha_http_global_init(void);
void alpha_http_global_cleanup(void);

/* The callbacks register curl's sockets/timer with the caller-owned LWS loop. */
alpha_err_t alpha_http_multi_create(alpha_http_watch_fn watch_fn, alpha_http_timer_fn timer_fn,
                                    alpha_http_complete_fn complete_fn, void *callback_ctx,
                                    alpha_http_multi_t **out);
void alpha_http_multi_destroy(alpha_http_multi_t *client);
size_t alpha_http_pending(const alpha_http_multi_t *client);

alpha_err_t alpha_http_post_json(alpha_http_multi_t *client, const alpha_http_post_t *post);

/* Drive only from the owner loop. watch is the readiness reported for socket_fd. */
alpha_err_t alpha_http_multi_socket_action(alpha_http_multi_t *client, int socket_fd,
                                           alpha_http_watch_t watch, int *running_out);
alpha_err_t alpha_http_multi_timeout_action(alpha_http_multi_t *client, int *running_out);

#endif
