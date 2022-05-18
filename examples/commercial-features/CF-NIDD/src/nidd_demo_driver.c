#include <anjay/anjay_config.h>
#include <avsystem/commons/avs_log.h>

#include <avsystem/commons/avs_buffer.h>
#include <avsystem/commons/avs_errno.h>

#include "nidd_demo_driver.h"

#include <anjay/bg96_nidd.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

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
    size_t space_left = avs_buffer_space_left(fifo->buffer);
    assert(space_left > 0); // This shall be handled in fifo_pop_line()
    ssize_t bytes_read =
            read(fd, avs_buffer_raw_insert_ptr(fifo->buffer), space_left);
    if (bytes_read < 0) {
        return avs_errno(AVS_EIO);
    } else if (bytes_read == 0) {
        return AVS_EOF;
    } else {
        assert((size_t) bytes_read <= space_left);
        avs_buffer_advance_ptr(fifo->buffer, (size_t) bytes_read);
        fifo_strip_nullbytes(fifo);
        return AVS_OK;
    }
}

typedef struct {
    anjay_nidd_driver_t *bg96_nidd;
    int pts_fd;
    fifo_t fifo;
} demo_nidd_driver_t;

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

static int modem_getline(void *user_context,
                         char *out_line_buffer,
                         size_t buffer_size,
                         avs_time_monotonic_t deadline) {
    demo_nidd_driver_t *driver = (demo_nidd_driver_t *) user_context;

    struct pollfd fd;
    fd.fd = driver->pts_fd;
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
            result = fifo_pop_line(&driver->fifo, out_line_buffer, buffer_size);
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
            avs_error_t err = fifo_push_read(&driver->fifo, driver->pts_fd);
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

static inline bool is_blank(const char *buffer) {
    for (const char *ch = buffer; *ch; ++ch) {
        if (!isspace(*ch)) {
            return false;
        }
    }
    return true;
}

static int modem_write(void *user_context, const char *buffer) {
    demo_nidd_driver_t *driver = (demo_nidd_driver_t *) user_context;
    // Note: not signal-safe.
    ssize_t written = write(driver->pts_fd, buffer, strlen(buffer));
    if (written != (ssize_t) strlen(buffer)) {
        return -1;
    }
    if (!is_blank(buffer)) {
        avs_log(tutorial, DEBUG, "[MODEM] sent: %s", buffer);
    }
    return 0;
}

static int modem_get_parameter(void *user_context,
                               anjay_bg96_nidd_parameter_t parameter,
                               char *out_value,
                               size_t size) {
    (void) user_context;
    static const char APN[] = "test";
    if (parameter == ANJAY_BG96_NIDD_APN) {
        if (size < sizeof(APN)) {
            return -1;
        }
        memcpy(out_value, APN, sizeof(APN));
        return 0;
    }
    *out_value = '\0';
    return 0;
}

static void driver_cleanup(demo_nidd_driver_t *driver) {
    if (driver->pts_fd >= 0) {
        close(driver->pts_fd);
    }
    anjay_nidd_driver_cleanup(&driver->bg96_nidd);
    fifo_destroy(&driver->fifo);
    avs_free(driver);
}

anjay_nidd_driver_t **demo_nidd_driver_create(const char *modem_device) {
    demo_nidd_driver_t *driver =
            (demo_nidd_driver_t *) avs_calloc(1, sizeof(*driver));
    if (!driver) {
        return NULL;
    }

    const anjay_bg96_nidd_config_t config = {
        .system_descriptor = &driver->pts_fd,
        .user_context = driver,
        .modem_getline = modem_getline,
        .modem_write = modem_write,
        .modem_get_parameter = modem_get_parameter
    };
    if (fifo_init(&driver->fifo)) {
        avs_log(tutorial, ERROR, "could not initialize FIFO");
        goto fail;
    }
    if ((driver->pts_fd = open(modem_device, O_RDWR)) < 0) {
        avs_log(tutorial, ERROR, "could not open modem device %s: %s",
                modem_device, strerror(errno));
        goto fail;
    }

    if (!(driver->bg96_nidd = anjay_bg96_nidd_driver_create(&config))) {
        avs_log(tutorial, ERROR, "could not create AT NIDD driver");
        goto fail;
    }
    return &driver->bg96_nidd;

fail:
    driver_cleanup(driver);
    return NULL;
}

void demo_nidd_driver_cleanup(anjay_nidd_driver_t **driver) {
    if (!driver || !*driver) {
        return;
    }
    demo_nidd_driver_t *demo_driver =
            AVS_CONTAINER_OF(driver, demo_nidd_driver_t, bg96_nidd);
    driver_cleanup(demo_driver);
}
