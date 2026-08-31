#include "alpha/lws_runtime.h"

#include <libwebsockets.h>

#include <stdlib.h>
#include <unistd.h>

struct alpha_lws_watch {
    struct alpha_lws_runtime *runtime;
    struct lws *wsi;
    alpha_lws_ready_fn ready_fn;
    void *callback_ctx;
    int source_fd;
    bool wants_write;
    bool active;
};

struct alpha_lws_runtime {
    struct lws_context *context;
    struct lws_vhost *vhost;
    alpha_lws_watch_t *watches;
    size_t capacity;
    size_t count;
    lws_sorted_usec_list_t wakeup;
};

static int foreign_fd_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user,
                               void *input, size_t length) {
    alpha_lws_watch_t *watch = lws_get_opaque_user_data(wsi);
    (void)user;
    (void)input;
    (void)length;
    if (watch == NULL) {
        return 0;
    }
    switch (reason) {
    case LWS_CALLBACK_RAW_RX_FILE:
        watch->ready_fn(watch->callback_ctx, true, false, false);
        break;
    case LWS_CALLBACK_RAW_WRITEABLE_FILE:
        if (watch->wants_write) {
            watch->ready_fn(watch->callback_ctx, false, true, false);
        }
        break;
    case LWS_CALLBACK_TIMER:
        watch->ready_fn(watch->callback_ctx, false, false, true);
        break;
    case LWS_CALLBACK_RAW_CLOSE_FILE:
        watch->wsi = NULL;
        break;
    default:
        break;
    }
    return 0;
}

static const struct lws_protocols PROTOCOLS[] = {
    {"alpha-driver-fd", foreign_fd_callback, 0, 0, 0, NULL, 0},
    LWS_PROTOCOL_LIST_TERM,
};

static void service_wakeup(lws_sorted_usec_list_t *scheduled) { (void)scheduled; }

alpha_err_t alpha_lws_runtime_create(size_t watch_capacity, alpha_lws_runtime_t **out) {
    if (watch_capacity == 0 || watch_capacity > 256 || out == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    *out = NULL;
    alpha_lws_runtime_t *runtime = calloc(1, sizeof(*runtime));
    if (runtime == NULL) {
        return ALPHA_ERR_IO;
    }
    runtime->watches = calloc(watch_capacity, sizeof(*runtime->watches));
    if (runtime->watches == NULL) {
        free(runtime);
        return ALPHA_ERR_IO;
    }
    runtime->capacity = watch_capacity;
    struct lws_context_creation_info info = {0};
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = PROTOCOLS;
    info.options = LWS_SERVER_OPTION_EXPLICIT_VHOSTS;
    runtime->context = lws_create_context(&info);
    if (runtime->context == NULL) {
        alpha_lws_runtime_destroy(runtime);
        return ALPHA_ERR_IO;
    }
    info.vhost_name = "alpha-drivers";
    runtime->vhost = lws_create_vhost(runtime->context, &info);
    if (runtime->vhost == NULL) {
        alpha_lws_runtime_destroy(runtime);
        return ALPHA_ERR_IO;
    }
    *out = runtime;
    return ALPHA_OK;
}

void alpha_lws_runtime_destroy(alpha_lws_runtime_t *runtime) {
    if (runtime == NULL) {
        return;
    }
    if (runtime->context != NULL) {
        for (size_t i = 0; i < runtime->capacity; ++i) {
            if (runtime->watches[i].wsi != NULL) {
                lws_wsi_close(runtime->watches[i].wsi, LWS_TO_KILL_SYNC);
            }
        }
        lws_context_destroy(runtime->context);
    }
    free(runtime->watches);
    free(runtime);
}

alpha_err_t alpha_lws_watch_add(alpha_lws_runtime_t *runtime, int fd, bool wants_write,
                                alpha_lws_ready_fn ready_fn, void *callback_ctx,
                                alpha_lws_watch_t **out) {
    if (runtime == NULL || fd < 0 || ready_fn == NULL || out == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    if (runtime->count == runtime->capacity) {
        return ALPHA_ERR_RANGE;
    }
    const int adopted_fd = dup(fd);
    if (adopted_fd < 0) {
        return ALPHA_ERR_IO;
    }
    alpha_lws_watch_t *watch = NULL;
    for (size_t i = 0; i < runtime->capacity; ++i) {
        if (!runtime->watches[i].active) {
            watch = &runtime->watches[i];
            break;
        }
    }
    if (watch == NULL) {
        close(adopted_fd);
        return ALPHA_ERR_RANGE;
    }
    *watch = (alpha_lws_watch_t){
        .runtime = runtime,
        .ready_fn = ready_fn,
        .callback_ctx = callback_ctx,
        .source_fd = fd,
        .wants_write = wants_write,
        .active = true,
    };
    const lws_adopt_desc_t descriptor = {
        .vh = runtime->vhost,
        .type = LWS_ADOPT_RAW_FILE_DESC,
        .fd = {.filefd = adopted_fd},
        .vh_prot_name = PROTOCOLS[0].name,
        .opaque = watch,
    };
    watch->wsi = lws_adopt_descriptor_vhost_via_info(&descriptor);
    if (watch->wsi == NULL) {
        watch->active = false;
        return ALPHA_ERR_IO;
    }
    runtime->count++;
    if (wants_write) {
        (void)lws_callback_on_writable(watch->wsi);
    }
    *out = watch;
    return ALPHA_OK;
}

alpha_err_t alpha_lws_watch_set_write(alpha_lws_watch_t *watch, bool wants_write) {
    if (watch == NULL || watch->wsi == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    watch->wants_write = wants_write;
    if (wants_write && lws_callback_on_writable(watch->wsi) < 0) {
        return ALPHA_ERR_IO;
    }
    return ALPHA_OK;
}

alpha_err_t alpha_lws_watch_set_timer(alpha_lws_watch_t *watch, int64_t timeout_ms) {
    if (watch == NULL || watch->wsi == NULL || timeout_ms < -1) {
        return ALPHA_ERR_INVALID_ARG;
    }
    const lws_usec_t usecs =
        timeout_ms < 0 ? LWS_SET_TIMER_USEC_CANCEL : (lws_usec_t)timeout_ms * LWS_US_PER_MS;
    lws_set_timer_usecs(watch->wsi, usecs);
    return ALPHA_OK;
}

void alpha_lws_watch_remove(alpha_lws_watch_t *watch) {
    if (watch == NULL || !watch->active) {
        return;
    }
    if (watch->wsi != NULL) {
        lws_wsi_close(watch->wsi, LWS_TO_KILL_SYNC);
    }
    watch->active = false;
    watch->runtime->count--;
}

int alpha_lws_watch_source_fd(const alpha_lws_watch_t *watch) {
    return watch != NULL ? watch->source_fd : -1;
}

alpha_err_t alpha_lws_runtime_service(alpha_lws_runtime_t *runtime, int timeout_ms) {
    if (runtime == NULL || timeout_ms < 0) {
        return ALPHA_ERR_INVALID_ARG;
    }
    const lws_usec_t maximum_wait = timeout_ms == 0 ? 1 : (lws_usec_t)timeout_ms * LWS_US_PER_MS;
    lws_sul_schedule(runtime->context, 0, &runtime->wakeup, service_wakeup, maximum_wait);
    return lws_service(runtime->context, 0) < 0 ? ALPHA_ERR_IO : ALPHA_OK;
}

size_t alpha_lws_runtime_watch_count(const alpha_lws_runtime_t *runtime) {
    return runtime != NULL ? runtime->count : 0;
}

size_t alpha_lws_runtime_watch_capacity(const alpha_lws_runtime_t *runtime) {
    return runtime != NULL ? runtime->capacity : 0;
}
