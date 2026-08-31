#include "alpha/lws_runtime.h"
#include "unity.h"

#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    alpha_lws_watch_t *watch;
    int readable;
    int writable;
    int timed_out;
} capture_t;

void setUp(void) {}
void tearDown(void) {}

static void captured(void *ctx, bool readable, bool writable, bool timed_out) {
    capture_t *capture = ctx;
    if (readable) {
        char byte = '\0';
        TEST_ASSERT_EQUAL_INT(1, read(alpha_lws_watch_source_fd(capture->watch), &byte, 1));
        TEST_ASSERT_EQUAL_INT('x', byte);
        capture->readable++;
    }
    if (writable) {
        capture->writable++;
        TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_lws_watch_set_write(capture->watch, false));
    }
    if (timed_out) {
        capture->timed_out++;
    }
}

static void test_foreign_fd_read_write_timer_bounds_and_ownership(void) {
    int pair[2] = {-1, -1};
    TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    alpha_lws_runtime_t *runtime = NULL;
    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_lws_runtime_create(1, &runtime));
    TEST_ASSERT_EQUAL_size_t(1, alpha_lws_runtime_watch_capacity(runtime));

    capture_t capture = {0};
    TEST_ASSERT_EQUAL_INT(
        ALPHA_OK, alpha_lws_watch_add(runtime, pair[0], true, captured, &capture, &capture.watch));
    TEST_ASSERT_EQUAL_size_t(1, alpha_lws_runtime_watch_count(runtime));
    alpha_lws_watch_t *rejected = NULL;
    TEST_ASSERT_EQUAL_INT(ALPHA_ERR_RANGE, alpha_lws_watch_add(runtime, pair[1], false, captured,
                                                               &capture, &rejected));

    for (int i = 0; i < 20 && capture.writable == 0; ++i) {
        TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_lws_runtime_service(runtime, 10));
    }
    TEST_ASSERT_EQUAL_INT(1, capture.writable);

    TEST_ASSERT_EQUAL_INT(1, write(pair[1], "x", 1));
    for (int i = 0; i < 20 && capture.readable == 0; ++i) {
        TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_lws_runtime_service(runtime, 10));
    }
    TEST_ASSERT_EQUAL_INT(1, capture.readable);

    TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_lws_watch_set_timer(capture.watch, 1));
    for (int i = 0; i < 20 && capture.timed_out == 0; ++i) {
        TEST_ASSERT_EQUAL_INT(ALPHA_OK, alpha_lws_runtime_service(runtime, 10));
    }
    TEST_ASSERT_EQUAL_INT(1, capture.timed_out);

    alpha_lws_runtime_destroy(runtime);
    TEST_ASSERT_EQUAL_INT(1, write(pair[0], "y", 1));
    char byte = '\0';
    TEST_ASSERT_EQUAL_INT(1, read(pair[1], &byte, 1));
    TEST_ASSERT_EQUAL_INT('y', byte);
    close(pair[0]);
    close(pair[1]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_foreign_fd_read_write_timer_bounds_and_ownership);
    return UNITY_END();
}
