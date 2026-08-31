#include "alpha/http_client.h"

#define CURL_DISABLE_TYPECHECK
#include <curl/curl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct alpha_http_request {
    CURL *easy;
    struct curl_slist *headers;
    char *body;
    char *response;
    size_t response_size;
    alpha_http_post_kind_t kind;
    void *user_data;
    struct alpha_http_request *next;
} alpha_http_request_t;

struct alpha_http_multi {
    CURLM *multi;
    alpha_http_watch_fn watch_fn;
    alpha_http_timer_fn timer_fn;
    alpha_http_complete_fn complete_fn;
    void *callback_ctx;
    alpha_http_request_t *requests;
    size_t pending;
};

static size_t receive_body(char *data, size_t size, size_t count, void *ctx) {
    alpha_http_request_t *request = ctx;
    const size_t bytes = size * count;
    if (bytes > ALPHA_HTTP_MAX_RESPONSE_BYTES - request->response_size ||
        bytes > SIZE_MAX - request->response_size - 1) {
        return 0;
    }
    char *next = realloc(request->response, request->response_size + bytes + 1);
    if (next == NULL) {
        return 0;
    }
    request->response = next;
    memcpy(request->response + request->response_size, data, bytes);
    request->response_size += bytes;
    request->response[request->response_size] = '\0';
    return bytes;
}

static int curl_socket_changed(CURL *easy, curl_socket_t socket_fd, int action, void *ctx,
                               void *socket_ctx) {
    (void)easy;
    (void)socket_ctx;
    alpha_http_multi_t *client = ctx;
    alpha_http_watch_t watch = ALPHA_HTTP_WATCH_NONE;
    if (action >= CURL_POLL_IN && action <= CURL_POLL_INOUT) {
        watch = (alpha_http_watch_t)action;
    }
    client->watch_fn(client->callback_ctx, (int)socket_fd, watch);
    return 0;
}

static int curl_timer_changed(CURLM *multi, long timeout_ms, void *ctx) {
    (void)multi;
    alpha_http_multi_t *client = ctx;
    client->timer_fn(client->callback_ctx, timeout_ms);
    return 0;
}

static void unlink_and_free(alpha_http_multi_t *client, alpha_http_request_t *request) {
    alpha_http_request_t **cursor = &client->requests;
    while (*cursor != NULL && *cursor != request) {
        cursor = &(*cursor)->next;
    }
    if (*cursor == request) {
        *cursor = request->next;
        client->pending--;
    }
    curl_slist_free_all(request->headers);
    curl_easy_cleanup(request->easy);
    free(request->body);
    free(request->response);
    free(request);
}

static void drain_completions(alpha_http_multi_t *client) {
    int queued = 0;
    CURLMsg *message = NULL;
    while ((message = curl_multi_info_read(client->multi, &queued)) != NULL) {
        if (message->msg != CURLMSG_DONE) {
            continue;
        }
        alpha_http_request_t *request = NULL;
        (void)curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &request);
        long status = 0;
        (void)curl_easy_getinfo(message->easy_handle, CURLINFO_RESPONSE_CODE, &status);
        (void)curl_multi_remove_handle(client->multi, message->easy_handle);
        const alpha_http_result_t result = {
            .kind = request->kind,
            .status_code = status,
            .transport_code = (int)message->data.result,
            .response_body = request->response != NULL ? request->response : "",
            .response_size = request->response_size,
            .user_data = request->user_data,
        };
        client->complete_fn(client->callback_ctx, &result);
        unlink_and_free(client, request);
    }
}

alpha_err_t alpha_http_global_init(void) {
    // NOLINTNEXTLINE(bugprone-signed-bitwise): libcurl owns this public flag macro.
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ? ALPHA_OK : ALPHA_ERR_HTTP;
}

void alpha_http_global_cleanup(void) { curl_global_cleanup(); }

alpha_err_t alpha_http_multi_create(alpha_http_watch_fn watch_fn, alpha_http_timer_fn timer_fn,
                                    alpha_http_complete_fn complete_fn, void *callback_ctx,
                                    alpha_http_multi_t **out) {
    if (watch_fn == NULL || timer_fn == NULL || complete_fn == NULL || out == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    *out = NULL;
    alpha_http_multi_t *client = calloc(1, sizeof(*client));
    if (client == NULL) {
        return ALPHA_ERR_IO;
    }
    client->multi = curl_multi_init();
    if (client->multi == NULL) {
        free(client);
        return ALPHA_ERR_HTTP;
    }
    client->watch_fn = watch_fn;
    client->timer_fn = timer_fn;
    client->complete_fn = complete_fn;
    client->callback_ctx = callback_ctx;
    if (curl_multi_setopt(client->multi, CURLMOPT_SOCKETFUNCTION, curl_socket_changed) !=
            CURLM_OK ||
        curl_multi_setopt(client->multi, CURLMOPT_SOCKETDATA, client) != CURLM_OK ||
        curl_multi_setopt(client->multi, CURLMOPT_TIMERFUNCTION, curl_timer_changed) != CURLM_OK ||
        curl_multi_setopt(client->multi, CURLMOPT_TIMERDATA, client) != CURLM_OK) {
        curl_multi_cleanup(client->multi);
        free(client);
        return ALPHA_ERR_HTTP;
    }
    *out = client;
    return ALPHA_OK;
}

void alpha_http_multi_destroy(alpha_http_multi_t *client) {
    if (client == NULL) {
        return;
    }
    while (client->requests != NULL) {
        alpha_http_request_t *request = client->requests;
        (void)curl_multi_remove_handle(client->multi, request->easy);
        unlink_and_free(client, request);
    }
    curl_multi_cleanup(client->multi);
    free(client);
}

size_t alpha_http_pending(const alpha_http_multi_t *client) {
    return client != NULL ? client->pending : 0;
}

alpha_err_t alpha_http_post_json(alpha_http_multi_t *client, const alpha_http_post_t *post) {
    if (client == NULL || post == NULL || post->url == NULL || post->url[0] == '\0' ||
        post->json_body == NULL || post->timeout_ms <= 0 ||
        post->kind < ALPHA_HTTP_POST_GENERIC_JSON || post->kind > ALPHA_HTTP_POST_LLM) {
        return ALPHA_ERR_INVALID_ARG;
    }
    if (client->pending >= ALPHA_HTTP_MAX_PENDING) {
        return ALPHA_ERR_RANGE;
    }
    alpha_http_request_t *request = calloc(1, sizeof(*request));
    if (request == NULL) {
        return ALPHA_ERR_IO;
    }
    request->easy = curl_easy_init();
    const size_t body_size = strlen(post->json_body);
    request->body = malloc(body_size + 1);
    if (request->body != NULL) {
        memcpy(request->body, post->json_body, body_size + 1);
    }
    request->kind = post->kind;
    request->user_data = post->user_data;
    if (request->easy == NULL || request->body == NULL) {
        unlink_and_free(client, request);
        return ALPHA_ERR_IO;
    }
    request->headers = curl_slist_append(NULL, "Content-Type: application/json");
    if (request->headers == NULL) {
        unlink_and_free(client, request);
        return ALPHA_ERR_IO;
    }
    if (post->authorization_header != NULL) {
        struct curl_slist *with_auth =
            curl_slist_append(request->headers, post->authorization_header);
        if (with_auth == NULL) {
            unlink_and_free(client, request);
            return ALPHA_ERR_IO;
        }
        request->headers = with_auth;
    }
    if (curl_easy_setopt(request->easy, CURLOPT_URL, post->url) != CURLE_OK ||
        curl_easy_setopt(request->easy, CURLOPT_POST, 1L) != CURLE_OK ||
        curl_easy_setopt(request->easy, CURLOPT_POSTFIELDS, request->body) != CURLE_OK ||
        curl_easy_setopt(request->easy, CURLOPT_POSTFIELDSIZE, (long)body_size) != CURLE_OK ||
        curl_easy_setopt(request->easy, CURLOPT_HTTPHEADER, request->headers) != CURLE_OK ||
        curl_easy_setopt(request->easy, CURLOPT_WRITEFUNCTION, receive_body) != CURLE_OK ||
        curl_easy_setopt(request->easy, CURLOPT_WRITEDATA, request) != CURLE_OK ||
        curl_easy_setopt(request->easy, CURLOPT_PRIVATE, request) != CURLE_OK ||
        curl_easy_setopt(request->easy, CURLOPT_TIMEOUT_MS, post->timeout_ms) != CURLE_OK ||
        curl_easy_setopt(request->easy, CURLOPT_NOSIGNAL, 1L) != CURLE_OK) {
        unlink_and_free(client, request);
        return ALPHA_ERR_HTTP;
    }
    request->next = client->requests;
    client->requests = request;
    client->pending++;
    if (curl_multi_add_handle(client->multi, request->easy) != CURLM_OK) {
        unlink_and_free(client, request);
        return ALPHA_ERR_HTTP;
    }
    return ALPHA_OK;
}

alpha_err_t alpha_http_multi_socket_action(alpha_http_multi_t *client, int socket_fd,
                                           alpha_http_watch_t watch, int *running_out) {
    const unsigned ready = (unsigned)watch;
    const unsigned allowed = (unsigned)ALPHA_HTTP_WATCH_READ | (unsigned)ALPHA_HTTP_WATCH_WRITE;
    if (client == NULL || socket_fd < 0 || running_out == NULL || (ready & ~allowed) != 0U) {
        return ALPHA_ERR_INVALID_ARG;
    }
    unsigned events = 0;
    if ((ready & (unsigned)ALPHA_HTTP_WATCH_READ) != 0U) {
        events |= (unsigned)CURL_CSELECT_IN;
    }
    if ((ready & (unsigned)ALPHA_HTTP_WATCH_WRITE) != 0U) {
        events |= (unsigned)CURL_CSELECT_OUT;
    }
    if (curl_multi_socket_action(client->multi, (curl_socket_t)socket_fd, (int)events,
                                 running_out) != CURLM_OK) {
        return ALPHA_ERR_HTTP;
    }
    drain_completions(client);
    return ALPHA_OK;
}

alpha_err_t alpha_http_multi_timeout_action(alpha_http_multi_t *client, int *running_out) {
    if (client == NULL || running_out == NULL) {
        return ALPHA_ERR_INVALID_ARG;
    }
    if (curl_multi_socket_action(client->multi, CURL_SOCKET_TIMEOUT, 0, running_out) != CURLM_OK) {
        return ALPHA_ERR_HTTP;
    }
    drain_completions(client);
    return ALPHA_OK;
}
