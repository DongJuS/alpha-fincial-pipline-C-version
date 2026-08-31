#include "alpha/http_client.h"
#include "unity.h"

#include <arpa/inet.h>
#include <curl/curl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int socket_fd;
    alpha_http_watch_t watch;
    long timeout_ms;
    int completions;
    alpha_http_post_kind_t kinds[8];
    long statuses[8];
    int transports[8];
    void *user_data[8];
    char bodies[8][32];
} loop_state_t;

void setUp(void) {}
void tearDown(void) {}

static void watch_socket(void *ctx, int socket_fd, alpha_http_watch_t watch) {
    loop_state_t *state = ctx;
    if (watch == ALPHA_HTTP_WATCH_NONE && state->socket_fd == socket_fd) {
        state->socket_fd = -1;
        state->watch = watch;
    } else if (watch != ALPHA_HTTP_WATCH_NONE) {
        state->socket_fd = socket_fd;
        state->watch = watch;
    }
}

static void schedule_timer(void *ctx, long timeout_ms) {
    loop_state_t *state = ctx;
    state->timeout_ms = timeout_ms;
}

static void completed(void *ctx, const alpha_http_result_t *result) {
    loop_state_t *state = ctx;
    const int index = state->completions++;
    if (index >= 8) {
        return;
    }
    state->kinds[index] = result->kind;
    state->statuses[index] = result->status_code;
    state->transports[index] = result->transport_code;
    state->user_data[index] = result->user_data;
    const size_t copied = result->response_size < sizeof(state->bodies[index]) - 1
                              ? result->response_size
                              : sizeof(state->bodies[index]) - 1;
    memcpy(state->bodies[index], result->response_body, copied);
    state->bodies[index][copied] = '\0';
}

static void drive_until(alpha_http_multi_t *http, loop_state_t *state, int expected) {
    int running = 0;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_http_multi_timeout_action(http, &running));
    for (int attempt = 0; attempt < 100 && state->completions < expected; ++attempt) {
        if (state->socket_fd < 0) {
            struct timespec pause = {.tv_sec = 0, .tv_nsec = 1000000L};
            (void)nanosleep(&pause, NULL);
            TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_http_multi_timeout_action(http, &running));
            continue;
        }
        struct pollfd descriptor = {
            .fd = state->socket_fd,
            .events = (short)(((state->watch & ALPHA_HTTP_WATCH_READ) ? POLLIN : 0) |
                              ((state->watch & ALPHA_HTTP_WATCH_WRITE) ? POLLOUT : 0))};
        const int timeout =
            state->timeout_ms >= 0 && state->timeout_ms < 100 ? (int)state->timeout_ms : 100;
        const int ready_count = poll(&descriptor, 1, timeout);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, ready_count);
        if (ready_count == 0) {
            TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_http_multi_timeout_action(http, &running));
            continue;
        }
        alpha_http_watch_t ready = ALPHA_HTTP_WATCH_NONE;
        if ((descriptor.revents & (POLLIN | POLLHUP | POLLERR)) != 0)
            ready = (alpha_http_watch_t)(ready | ALPHA_HTTP_WATCH_READ);
        if ((descriptor.revents & POLLOUT) != 0)
            ready = (alpha_http_watch_t)(ready | ALPHA_HTTP_WATCH_WRITE);
        TEST_ASSERT_EQUAL_INT(ALPHA_OK,
                              alpha_http_multi_socket_action(http, descriptor.fd, ready, &running));
    }
    TEST_ASSERT_EQUAL_INT(expected, state->completions);
}

static int make_server(uint16_t *port_out) {
    const int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        return -1;
    }
    const int enabled = 1;
    (void)setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    struct sockaddr_in address = {
        .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK), .sin_port = 0};
    socklen_t length = sizeof(address);
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        getsockname(server, (struct sockaddr *)&address, &length) != 0 || listen(server, 2) != 0) {
        close(server);
        return -1;
    }
    *port_out = ntohs(address.sin_port);
    return server;
}

static int serve_requests(int server) {
    for (int index = 0; index < 2; ++index) {
        const int client = accept(server, NULL, NULL);
        if (client < 0) {
            return 10;
        }
        char request[4096] = {0};
        size_t used = 0;
        while (used < sizeof(request) - 1) {
            const ssize_t received = read(client, request + used, sizeof(request) - 1 - used);
            if (received <= 0) {
                break;
            }
            used += (size_t)received;
            request[used] = '\0';
            char *headers_end = strstr(request, "\r\n\r\n");
            if (headers_end != NULL) {
                size_t content_length = 0;
                char *length_header = strstr(request, "Content-Length:");
                if (length_header != NULL) {
                    content_length = (size_t)strtoul(length_header + 15, NULL, 10);
                }
                if (used >= (size_t)(headers_end + 4 - request) + content_length) {
                    break;
                }
            }
        }
        const int valid = strstr(request, "POST /replay HTTP/") != NULL &&
                          strstr(request, "Content-Type: application/json") != NULL &&
                          strstr(request, "Authorization: Bearer fixture") != NULL &&
                          ((index == 0 && strstr(request, "{\"chat_id\":1}") != NULL) ||
                           (index == 1 && strstr(request, "{\"model\":\"fixture\"}") != NULL));
        const char *body = index == 0 ? "{\"ok\":true}" : "{\"reply\":\"fixed\"}";
        char response[256];
        const int response_size = snprintf(response, sizeof(response),
                                           "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                                           "Content-Length: %zu\r\nConnection: close\r\n\r\n%s",
                                           strlen(body), body);
        if (!valid || write(client, response, (size_t)response_size) != response_size) {
            close(client);
            return 11 + index;
        }
        close(client);
    }
    close(server);
    return 0;
}

static int serve_failure_matrix(int server) {
    (void)signal(SIGPIPE, SIG_IGN);
    for (int index = 0; index < 3; ++index) {
        const int client = accept(server, NULL, NULL);
        if (client < 0) {
            return 20;
        }
        char request[2048] = {0};
        const ssize_t received = read(client, request, sizeof(request) - 1);
        if (received <= 0) {
            close(client);
            return 21;
        }
        if (index == 1) {
            struct timespec pause = {.tv_sec = 0, .tv_nsec = 200000000L};
            (void)nanosleep(&pause, NULL);
        }
        const int status = index == 0 ? 503 : 200;
        const char *reason = index == 0 ? "Unavailable" : "OK";
        const char *body = index == 0 ? "{\"error\":true}" : "{\"ok\":true}";
        char response[256];
        const int response_size =
            snprintf(response, sizeof(response),
                     "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n"
                     "Connection: close\r\n\r\n%s",
                     status, reason, strlen(body), body);
        (void)write(client, response, (size_t)response_size);
        close(client);
    }
    close(server);
    return 0;
}

static void test_two_generic_json_edges_use_caller_driven_loop(void) {
    uint16_t port = 0;
    const int server = make_server(&port);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, server);
    const pid_t child = fork();
    TEST_ASSERT_NOT_EQUAL(-1, child);
    if (child == 0) {
        _exit(serve_requests(server));
    }
    close(server);

    loop_state_t state = {.socket_fd = -1, .timeout_ms = -1};
    alpha_http_multi_t *http = NULL;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_http_global_init());
    TEST_ASSERT_EQUAL_INT(
        ALPHA_OK, alpha_http_multi_create(watch_socket, schedule_timer, completed, &state, &http));
    char url[128];
    (void)snprintf(url, sizeof(url), "http://127.0.0.1:%u/replay", (unsigned)port);
    const alpha_http_post_t telegram = {.kind = ALPHA_HTTP_POST_TELEGRAM,
                                        .url = url,
                                        .json_body = "{\"chat_id\":1}",
                                        .authorization_header = "Authorization: Bearer fixture",
                                        .timeout_ms = 2000};
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_http_post_json(http, &telegram));

    int running = 0;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_http_multi_timeout_action(http, &running));
    while (state.completions < 1) {
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, state.socket_fd);
        struct pollfd descriptor = {
            .fd = state.socket_fd,
            .events = (short)(((state.watch & ALPHA_HTTP_WATCH_READ) ? POLLIN : 0) |
                              ((state.watch & ALPHA_HTTP_WATCH_WRITE) ? POLLOUT : 0))};
        TEST_ASSERT_GREATER_THAN_INT(0, poll(&descriptor, 1, 2000));
        alpha_http_watch_t ready = ALPHA_HTTP_WATCH_NONE;
        if ((descriptor.revents & POLLIN) != 0)
            ready = (alpha_http_watch_t)(ready | ALPHA_HTTP_WATCH_READ);
        if ((descriptor.revents & POLLOUT) != 0)
            ready = (alpha_http_watch_t)(ready | ALPHA_HTTP_WATCH_WRITE);
        TEST_ASSERT_EQUAL_INT(ALPHA_OK,
                              alpha_http_multi_socket_action(http, descriptor.fd, ready, &running));
    }

    const alpha_http_post_t llm = {.kind = ALPHA_HTTP_POST_LLM,
                                   .url = url,
                                   .json_body = "{\"model\":\"fixture\"}",
                                   .authorization_header = "Authorization: Bearer fixture",
                                   .timeout_ms = 2000};
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_http_post_json(http, &llm));
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_http_multi_timeout_action(http, &running));
    while (state.completions < 2) {
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, state.socket_fd);
        struct pollfd descriptor = {
            .fd = state.socket_fd,
            .events = (short)(((state.watch & ALPHA_HTTP_WATCH_READ) ? POLLIN : 0) |
                              ((state.watch & ALPHA_HTTP_WATCH_WRITE) ? POLLOUT : 0))};
        TEST_ASSERT_GREATER_THAN_INT(0, poll(&descriptor, 1, 2000));
        alpha_http_watch_t ready = ALPHA_HTTP_WATCH_NONE;
        if ((descriptor.revents & POLLIN) != 0)
            ready = (alpha_http_watch_t)(ready | ALPHA_HTTP_WATCH_READ);
        if ((descriptor.revents & POLLOUT) != 0)
            ready = (alpha_http_watch_t)(ready | ALPHA_HTTP_WATCH_WRITE);
        TEST_ASSERT_EQUAL_INT(ALPHA_OK,
                              alpha_http_multi_socket_action(http, descriptor.fd, ready, &running));
    }
    TEST_ASSERT_EQUAL_INT(ALPHA_HTTP_POST_TELEGRAM, state.kinds[0]);
    TEST_ASSERT_EQUAL_INT(200, state.statuses[0]);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", state.bodies[0]);
    TEST_ASSERT_EQUAL_INT(ALPHA_HTTP_POST_LLM, state.kinds[1]);
    TEST_ASSERT_EQUAL_INT(200, state.statuses[1]);
    TEST_ASSERT_EQUAL_STRING("{\"reply\":\"fixed\"}", state.bodies[1]);

    alpha_http_multi_destroy(http);
    alpha_http_global_cleanup();
    int child_status = 0;
    TEST_ASSERT_EQUAL_INT(child, waitpid(child, &child_status, 0));
    TEST_ASSERT_TRUE(WIFEXITED(child_status));
    TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(child_status));
}

static void test_pending_queue_is_bounded(void) {
    loop_state_t state = {.socket_fd = -1, .timeout_ms = -1};
    alpha_http_multi_t *http = NULL;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_http_global_init());
    TEST_ASSERT_EQUAL_INT(
        ALPHA_OK, alpha_http_multi_create(watch_socket, schedule_timer, completed, &state, &http));
    const alpha_http_post_t post = {.kind = ALPHA_HTTP_POST_GENERIC_JSON,
                                    .url = "http://127.0.0.1:1/unreachable",
                                    .json_body = "{}",
                                    .timeout_ms = 1000};
    for (size_t i = 0; i < ALPHA_HTTP_MAX_PENDING; ++i) {
        TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_http_post_json(http, &post));
    }
    TEST_ASSERT_EQUAL_size_t(ALPHA_HTTP_MAX_PENDING, alpha_http_pending(http));
    TEST_ASSERT_EQUAL_INT(ALPHA_ERR_RANGE, alpha_http_post_json(http, &post));
    alpha_http_multi_destroy(http);
    alpha_http_global_cleanup();
}

static void test_failure_matrix_cancel_and_reuse_preserve_identity(void) {
    uint16_t port = 0;
    const int server = make_server(&port);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, server);
    const pid_t child = fork();
    TEST_ASSERT_NOT_EQUAL(-1, child);
    if (child == 0) {
        _exit(serve_failure_matrix(server));
    }
    close(server);

    loop_state_t state = {.socket_fd = -1, .timeout_ms = -1};
    alpha_http_multi_t *http = NULL;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_http_global_init());
    TEST_ASSERT_EQUAL_INT(
        ALPHA_OK, alpha_http_multi_create(watch_socket, schedule_timer, completed, &state, &http));
    char url[128];
    (void)snprintf(url, sizeof(url), "http://127.0.0.1:%u/failure", (unsigned)port);
    int ids[] = {101, 102, 103, 104, 105};

    alpha_http_post_t post = {.kind = ALPHA_HTTP_POST_GENERIC_JSON,
                              .url = url,
                              .json_body = "{}",
                              .timeout_ms = 1000,
                              .user_data = &ids[0]};
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_http_post_json(http, &post));
    drive_until(http, &state, 1);
    TEST_ASSERT_EQUAL_INT(503, state.statuses[0]);
    TEST_ASSERT_EQUAL_INT(CURLE_OK, state.transports[0]);
    TEST_ASSERT_EQUAL_PTR(&ids[0], state.user_data[0]);

    post.timeout_ms = 25;
    post.user_data = &ids[1];
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_http_post_json(http, &post));
    drive_until(http, &state, 2);
    TEST_ASSERT_EQUAL_INT(CURLE_OPERATION_TIMEDOUT, state.transports[1]);
    TEST_ASSERT_EQUAL_PTR(&ids[1], state.user_data[1]);

    post.timeout_ms = 1000;
    post.user_data = &ids[2];
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_http_post_json(http, &post));
    drive_until(http, &state, 3);
    TEST_ASSERT_EQUAL_INT(200, state.statuses[2]);
    TEST_ASSERT_EQUAL_INT(CURLE_OK, state.transports[2]);

    post.user_data = &ids[3];
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_http_post_json(http, &post));
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_http_cancel(http, &ids[3]));
    TEST_ASSERT_EQUAL_INT(CURLE_ABORTED_BY_CALLBACK, state.transports[3]);
    TEST_ASSERT_EQUAL_PTR(&ids[3], state.user_data[3]);
    TEST_ASSERT_EQUAL_size_t(0, alpha_http_pending(http));

    /* A refused connection completes as transport failure without poisoning the multi handle. */
    post.url = "http://127.0.0.1:1/refused";
    post.timeout_ms = 100;
    post.user_data = &ids[4];
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_http_post_json(http, &post));
    drive_until(http, &state, 5);
    TEST_ASSERT_NOT_EQUAL(CURLE_OK, state.transports[4]);
    TEST_ASSERT_EQUAL_PTR(&ids[4], state.user_data[4]);
    TEST_ASSERT_EQUAL_size_t(0, alpha_http_pending(http));

    alpha_http_multi_destroy(http);
    alpha_http_global_cleanup();
    int child_status = 0;
    TEST_ASSERT_EQUAL_INT(child, waitpid(child, &child_status, 0));
    TEST_ASSERT_TRUE(WIFEXITED(child_status));
    TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(child_status));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_two_generic_json_edges_use_caller_driven_loop);
    RUN_TEST(test_pending_queue_is_bounded);
    RUN_TEST(test_failure_matrix_cancel_and_reuse_preserve_identity);
    return UNITY_END();
}
