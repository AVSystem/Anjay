#include <anjay/anjay.h>
#include <anjay/security.h>
#include <anjay/server.h>
#include <anjay/sim_bootstrap.h>
#include <avsystem/commons/avs_buffer.h>
#include <avsystem/commons/avs_log.h>
#include <avsystem/commons/avs_stream_file.h>

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#define SIM_COMMAND_MAX_BINARY_SIZE 258

#define CSIM_RESP "+CSIM: "

#define REQ_BUF_SIZE \
    (sizeof("AT+CSIM=999,\"\"\r\n") + 2 * SIM_COMMAND_MAX_BINARY_SIZE)
#define RESP_BUF_SIZE \
    (sizeof(CSIM_RESP "999,\"\"") + 2 * SIM_COMMAND_MAX_BINARY_SIZE)

typedef struct {
    avs_buffer_t *buffer;
} fifo_t;

static int fifo_init(fifo_t *fifo) {
    if (fifo->buffer) {
        return -1;
    }
    return avs_buffer_create(&fifo->buffer, 4096);
}

static void fifo_destroy(fifo_t *fifo) {
    avs_buffer_free(&fifo->buffer);
}

static int fifo_find_off(fifo_t *fifo, char ch, size_t *out_off) {
    const char *start = avs_buffer_data(fifo->buffer);
    char *ptr = (char *) memchr(start, ch, avs_buffer_data_size(fifo->buffer));
    if (ptr) {
        *out_off = (size_t) (ptr - start);
        return 0;
    }
    return -1;
}

static void fifo_pop_n(fifo_t *fifo, char *out_buffer, size_t size, size_t n) {
    assert(n <= avs_buffer_data_size(fifo->buffer));
    assert(n <= size);
    (void) size;

    memcpy(out_buffer, avs_buffer_data(fifo->buffer), n);
    avs_buffer_consume_bytes(fifo->buffer, n);
}

static void fifo_discard_n(fifo_t *fifo, size_t n) {
    assert(n <= avs_buffer_data_size(fifo->buffer));
    avs_buffer_consume_bytes(fifo->buffer, n);
}

// NOTE: If out_line is too small to hold the entire line,
// excess characters will be discarded
static int fifo_pop_line(fifo_t *fifo, char *out_line, size_t out_size) {
    assert(out_size > 0);

    size_t line_size;
    if (!fifo_find_off(fifo, '\n', &line_size)
            || !fifo_find_off(fifo, '\r', &line_size)) {
        ++line_size;
    } else if (avs_buffer_space_left(fifo->buffer) == 0) {
        avs_log(tutorial, WARNING,
                "FIFO buffer full, treating received data as a line");
        line_size = avs_buffer_data_size(fifo->buffer);
    } else {
        line_size = 0;
    }

    size_t bytes_to_pop = AVS_MIN(out_size - 1, line_size);
    fifo_pop_n(fifo, out_line, out_size - 1, bytes_to_pop);
    out_line[bytes_to_pop] = '\0';

    // Discard excess bytes, if any
    if (line_size != bytes_to_pop) {
        fifo_discard_n(fifo, line_size - bytes_to_pop);
        avs_log(tutorial, WARNING, "buffer size too small to hold the line");
        return 1;
    }
    return 0;
}

static void fifo_strip_nullbytes(fifo_t *fifo) {
    size_t buffer_size = avs_buffer_data_size(fifo->buffer);
    char *buffer = avs_buffer_raw_insert_ptr(fifo->buffer) - buffer_size;
    ssize_t block_end_offset = (ssize_t) buffer_size;
    assert(block_end_offset >= 0);
    ssize_t moved_by = 0;
    while (block_end_offset > 0) {
        ssize_t first_null_offset = block_end_offset;
        while (first_null_offset > 0 && buffer[first_null_offset - 1] == '\0') {
            --first_null_offset;
        }
        ssize_t first_nonnull_offset = first_null_offset;
        while (first_nonnull_offset > 0
               && buffer[first_nonnull_offset - 1] != '\0') {
            --first_nonnull_offset;
        }
        if (first_null_offset != block_end_offset) {
            assert(first_null_offset < block_end_offset);
            moved_by += block_end_offset - first_null_offset;
            if (first_nonnull_offset != first_null_offset) {
                assert(first_nonnull_offset < first_null_offset);
                memmove(buffer + first_nonnull_offset + moved_by,
                        buffer + first_nonnull_offset,
                        (size_t) (first_null_offset - first_nonnull_offset));
            }
        }
        block_end_offset = first_nonnull_offset;
    }
    assert(moved_by >= 0);
    if (moved_by > 0) {
        avs_buffer_consume_bytes(fifo->buffer, (size_t) moved_by);
    }
}

static avs_error_t fifo_push_read(fifo_t *fifo, int fd) {
    // This shall be handled in fifo_pop_line()
    assert(avs_buffer_space_left(fifo->buffer) > 0);
    ssize_t bytes_read = read(fd, avs_buffer_raw_insert_ptr(fifo->buffer), 1);
    if (bytes_read < 0) {
        return avs_errno(AVS_EIO);
    } else if (bytes_read == 0) {
        return AVS_EOF;
    } else {
        assert(bytes_read == 1);
        avs_buffer_advance_ptr(fifo->buffer, 1);
        fifo_strip_nullbytes(fifo);
        return AVS_OK;
    }
}

typedef struct {
    fifo_t fifo;
    int pts_fd;
} modem_ctx_t;

static void trim_inplace(char *buffer) {
    assert(buffer);

    size_t len = strlen(buffer);
    for (char *ch = buffer + len - 1;
         ch >= buffer && isspace((unsigned char) *ch);
         --ch) {
        *ch = '\0';
    }
    char *first_nonblank = buffer;
    for (char *ch = buffer; *ch; ++ch) {
        if (!isspace((unsigned char) *ch)) {
            first_nonblank = ch;
            break;
        }
    }
    memmove(buffer, first_nonblank, strlen(first_nonblank) + 1);
}

static int modem_getline(modem_ctx_t *modem_ctx,
                         char *out_line_buffer,
                         size_t buffer_size,
                         avs_time_monotonic_t deadline) {
    struct pollfd fd;
    fd.fd = modem_ctx->pts_fd;
    fd.events = POLLIN;

    int64_t timeout_ms;
    int result;

    // Note: this loop is not signal-safe.
    do {
        if (avs_time_duration_to_scalar(
                    &timeout_ms, AVS_TIME_MS,
                    avs_time_monotonic_diff(deadline,
                                            avs_time_monotonic_now()))) {
            timeout_ms = -1;
        } else if (timeout_ms < 0) {
            timeout_ms = 0;
        }

        while (true) {
            result = fifo_pop_line(&modem_ctx->fifo, out_line_buffer,
                                   buffer_size);
            if (*out_line_buffer == '\0') {
                break;
            }
            trim_inplace(out_line_buffer);
            if (*out_line_buffer != '\0') {
                avs_log(tutorial, DEBUG, "[MODEM] recv: %s", out_line_buffer);
                return result;
            }
        }

        assert(timeout_ms <= INT_MAX);
        if ((result = poll(&fd, 1, (int) timeout_ms)) == 1) {
            avs_error_t err =
                    fifo_push_read(&modem_ctx->fifo, modem_ctx->pts_fd);
            if (avs_is_eof(err)) {
                avs_log(tutorial, DEBUG, "[MODEM] recv: EOF");
                return -1;
            } else if (avs_is_err(err)) {
                return -1;
            }
        } else if (result < 0) {
            return -1;
        }
        // read up until timeout becomes 0, or if it is 0, read up until there's
        // something to read.
    } while (timeout_ms != 0 || result == 1);

    avs_log(tutorial, DEBUG, "[MODEM] recv: timeout");
    assert(buffer_size > 0);
    out_line_buffer[0] = '\0';
    return 0;
}

static int sim_perform_command(void *modem_ctx_,
                               const void *cmd,
                               size_t cmd_length,
                               void *out_buf,
                               size_t out_buf_size,
                               size_t *out_response_size) {
    modem_ctx_t *modem_ctx = (modem_ctx_t *) modem_ctx_;
    char req_buf[REQ_BUF_SIZE];
    char resp_buf[RESP_BUF_SIZE] = "";

    char *req_buf_ptr = req_buf;
    char *const req_buf_end = req_buf + sizeof(req_buf);
    int result = avs_simple_snprintf(req_buf_ptr,
                                     (size_t) (req_buf_end - req_buf_ptr),
                                     "AT+CSIM=%" PRIu32 ",\"",
                                     (uint32_t) (2 * cmd_length));
    if (result < 0) {
        return result;
    }
    req_buf_ptr += result;
    if ((size_t) (req_buf_end - req_buf_ptr) < 2 * cmd_length) {
        return -1;
    }
    if ((result = avs_hexlify(req_buf_ptr, (size_t) (req_buf_end - req_buf_ptr),
                              NULL, cmd, cmd_length))) {
        return result;
    }
    req_buf_ptr += 2 * cmd_length;
    if ((result = avs_simple_snprintf(
                 req_buf_ptr, (size_t) (req_buf_end - req_buf_ptr), "\"\r\n"))
            < 0) {
        return result;
    }
    req_buf_ptr += result;
    ssize_t written =
            write(modem_ctx->pts_fd, req_buf, (size_t) (req_buf_ptr - req_buf));
    if (written != (ssize_t) (req_buf_ptr - req_buf)) {
        return -1;
    }
    avs_time_monotonic_t deadline = avs_time_monotonic_add(
            avs_time_monotonic_now(),
            avs_time_duration_from_scalar(5, AVS_TIME_S));
    bool csim_resp_received = false;
    bool ok_received = false;
    while (!ok_received) {
        if (modem_getline(modem_ctx, resp_buf, sizeof(resp_buf), deadline)) {
            return -1;
        }
        const char *resp_terminator = memchr(resp_buf, '\0', sizeof(resp_buf));
        if (!resp_terminator) {
            return -1;
        }
        if (memcmp(resp_buf, CSIM_RESP, strlen(CSIM_RESP)) == 0) {
            if (csim_resp_received) {
                return -1;
            }
            errno = 0;
            char *endptr = NULL;
            long long resp_reported_length =
                    strtoll(resp_buf + strlen(CSIM_RESP), &endptr, 10);
            if (errno || !endptr || endptr[0] != ',' || endptr[1] != '"'
                    || resp_reported_length < 0
                    || endptr + resp_reported_length + 2 >= resp_terminator
                    || endptr[resp_reported_length + 2] != '"'
                    || avs_unhexlify(out_response_size, (uint8_t *) out_buf,
                                     out_buf_size, endptr + 2,
                                     (size_t) resp_reported_length)) {
                return -1;
            }
            csim_resp_received = true;
        } else if (strcmp(resp_buf, "OK") == 0) {
            ok_received = true;
        }
    }
    return csim_resp_received ? 0 : -1;
}

static int bootstrap_from_sim(anjay_t *anjay, const char *modem_device) {
    modem_ctx_t modem_ctx = {
        .pts_fd = -1
    };
    int result = -1;

    avs_log(tutorial, INFO, "Attempting to bootstrap from SIM card");

    if (fifo_init(&modem_ctx.fifo)) {
        avs_log(tutorial, ERROR, "could not initialize FIFO");
        goto finish;
    }
    if ((modem_ctx.pts_fd = open(modem_device, O_RDWR)) < 0) {
        avs_log(tutorial, ERROR, "could not open modem device %s: %s",
                modem_device, strerror(errno));
        goto finish;
    }
    if (avs_is_err(anjay_sim_bootstrap_perform(anjay, sim_perform_command,
                                               &modem_ctx))) {
        avs_log(tutorial, ERROR, "Could not bootstrap from SIM card");
        goto finish;
    }
    result = 0;
finish:
    if (modem_ctx.pts_fd >= 0) {
        close(modem_ctx.pts_fd);
    }
    fifo_destroy(&modem_ctx.fifo);
    return result;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        avs_log(tutorial, ERROR, "usage: %s ENDPOINT_NAME MODEM_PATH", argv[0]);
        return -1;
    }

    const anjay_configuration_t CONFIG = {
        .endpoint_name = argv[1],
        .in_buffer_size = 4000,
        .out_buffer_size = 4000,
        .msg_cache_size = 4000
    };

    anjay_t *anjay = anjay_new(&CONFIG);
    if (!anjay) {
        avs_log(tutorial, ERROR, "Could not create Anjay object");
        return -1;
    }

    int result = 0;
    // Setup necessary objects
    if (anjay_security_object_install(anjay)
            || anjay_server_object_install(anjay)) {
        result = -1;
    }

    if (!result) {
        result = bootstrap_from_sim(anjay, argv[2]);
    }

    if (!result) {
        result = anjay_event_loop_run(
                anjay, avs_time_duration_from_scalar(1, AVS_TIME_S));
    }

    anjay_delete(anjay);
    return result;
}
