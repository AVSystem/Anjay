/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "wget_downloader.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <poll.h>

struct wget_context_struct {
    wget_callback_t *finish_callback;
    void* callback_data;
    wget_callback_data_deleter_t *callback_data_deleter;

    const iosched_entry_t *iosched_entry;
    FILE *pipe;
    char *save_path;
    iosched_t *iosched;
    wget_result_t result;
    struct timespec download_start_time;
};

wget_context_t *wget_context_new(iosched_t *iosched) {
    wget_context_t *retval =
        (wget_context_t *) calloc(1, sizeof(wget_context_t));
    if (retval) {
        retval->iosched = iosched;
    }
    return retval;
}

static void free_callback_data(void *ctx_) {
    wget_context_t *ctx = (wget_context_t *)ctx_;

    if (ctx->callback_data_deleter) {
        ctx->callback_data_deleter(ctx->callback_data);
    }

    ctx->callback_data_deleter = NULL;
}

static void remove_download_task(wget_context_t *ctx) {
    if (!ctx->iosched) {
        return;
    }
    if (ctx->iosched_entry) {
        iosched_entry_remove(ctx->iosched, ctx->iosched_entry);
        ctx->iosched_entry = NULL;
    }

    free_callback_data(ctx);
}

void wget_context_delete(wget_context_t **ctx) {
    if (!ctx || !*ctx) {
        return;
    }
    remove_download_task(*ctx);
    if ((*ctx)->pipe) {
        demo_log(INFO, "wget has been forcefully stopped");
        (void) pclose((*ctx)->pipe);
    }
    free((*ctx)->save_path);
    free(*ctx);
    *ctx = NULL;
}

int wget_register_finish_callback(wget_context_t *ctx,
                                  wget_callback_t *callback,
                                  void *data,
                                  wget_callback_data_deleter_t *data_deleter) {
    assert(ctx && callback);
    ctx->finish_callback = callback;
    ctx->callback_data = data;
    ctx->callback_data_deleter = data_deleter;
    return 0;
}

static wget_result_t exit_status_to_wget_result(int exit_status) {
    wget_result_t result = WGET_RESULT_ERR_GENERIC;
    if (exit_status >= WGET_RESULT_MIN_ && exit_status <= WGET_RESULT_MAX_) {
        result = (wget_result_t) exit_status;
    }
    return result;
}

static int gather_download_stats(wget_context_t *ctx,
                                 wget_download_stats_t *out_stats) {
    struct stat stats;
    if (!stat(ctx->save_path, &stats)) {
        out_stats->beg = ctx->download_start_time;
        out_stats->end = stats.st_mtim;
        if (stats.st_size < 0) {
            return -1;
        } else {
            out_stats->bytes_written = (uint64_t) stats.st_size;
        }
        return 0;
    }
    return -1;
}

static void after_download(short revents, void *args) {
    (void) revents;
    wget_context_t *ctx = (wget_context_t *) args;

    if (getc(ctx->pipe) != EOF) {
        demo_log(WARNING, "unexpected wget output after download finished");
    }

    if (ctx->pipe) {
        int status = pclose(ctx->pipe);
        int exit_status = INT_MIN;
        ctx->pipe = NULL;

        if (WIFEXITED(status)) {
            exit_status = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            exit_status = -WTERMSIG(status);
        }
        demo_log(INFO, "wget exit status: %d", exit_status);
        ctx->result = exit_status_to_wget_result(exit_status);
    } else {
        ctx->result = WGET_RESULT_ERR_GENERIC;
    }
    if (ctx->finish_callback) {
        wget_download_stats_t stats, *stats_ptr = NULL;
        if (!gather_download_stats(ctx, &stats)) {
            stats_ptr = &stats;
        }
        ctx->finish_callback(ctx->result, stats_ptr, ctx->callback_data);
    }
    remove_download_task(ctx);
}

#define WGET_CMD_FORMAT \
    "wget --no-use-server-timestamps --quiet --output-document '%s' '%s'"

static int make_wget_cmd(const char *url,
                         const char *path,
                         char *out_command,
                         size_t buf_size) {
    ssize_t result = snprintf(out_command, buf_size, WGET_CMD_FORMAT,
                              path, url);
    if (result < 0 || result >= (ssize_t) buf_size) {
        return -1;
    }
    return 0;
}

int wget_background_download(wget_context_t *ctx,
                             const char *url,
                             const char *path) {
    if (ctx->pipe) {
        return -1;
    }

    char command[strlen(url) + strlen(path) + 256];
    if (make_wget_cmd(url, path, command, sizeof(command))) {
        return -1;
    }
    ctx->save_path = strdup(path);
    if (!ctx->save_path) {
        return -1;
    }

    demo_log(INFO, "scheduling download command: %s", command);
    ctx->pipe = popen(command, "r");
    if (!ctx->pipe) {
        demo_log(ERROR, "could not start download: %s", command);
        return -1;
    }
    clock_gettime(CLOCK_REALTIME, &ctx->download_start_time);
    const int wget_fd = fileno(ctx->pipe);
    ctx->iosched_entry = iosched_poll_entry_new(ctx->iosched, wget_fd, POLLIN,
                                                after_download, ctx,
                                                free_callback_data);
    if (!ctx->iosched_entry) {
        return -1;
    }
    return 0;
}


