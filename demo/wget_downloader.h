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

#ifndef DEMO_WGET_DOWNLOADER_H
#define DEMO_WGET_DOWNLOADER_H

#include <avsystem/commons/vector.h>
#include "iosched.h"
#include <stdio.h>
#include <sys/time.h>

typedef enum {
    /* Exit codes from wget manpage. */
    WGET_RESULT_OK          = 0,
    /* Generic error code. */
    WGET_RESULT_ERR_GENERIC = 1,
    /* Parse error---for instance, when parsing command-line options,
     * the .wgetrc or .netrc... */
    WGET_RESULT_ERR_PARSE   = 2,
    /* File I/O error. */
    WGET_RESULT_ERR_IO      = 3,
    /* Network failure. */
    WGET_RESULT_ERR_NET     = 4,
    /* SSL verification failure. */
    WGET_RESULT_ERR_SSL     = 5,
    /* Username/password authentication failure. */
    WGET_RESULT_ERR_AUTH    = 6,
    /* Protocol errors. */
    WGET_RESULT_ERR_PROTO   = 7,
    /* Server issued an error response. */
    WGET_RESULT_ERR_SERVER  = 8,

    WGET_RESULT_MIN_ = 0,
    WGET_RESULT_MAX_ = 8
} wget_result_t;

typedef struct {
    struct timespec beg;
    struct timespec end;
    uint64_t bytes_written;
} wget_download_stats_t;

/**
 * Warning: @p stats is valid only in callback scope. Also
 * note that @p stats might be NULL if there was a problem
 * while obtaining them.
 */
typedef void wget_callback_t(wget_result_t result,
                             const wget_download_stats_t *stats,
                             void *data);
typedef void wget_callback_data_deleter_t(void *data);

typedef struct wget_context_struct wget_context_t;


/**
 * Creates new wget context.
 */
wget_context_t *wget_context_new(iosched_t *iosched);

/**
 * Frees memory associated with context, and sets *ctx to NULL.
 */
void wget_context_delete(wget_context_t **ctx);

/**
 * Registers callback that will be executed after download finishes.
 * Note: this function returns an error value if callback is already
 * registered
 * @param ctx          Initialized wget context.
 * @param callback     Non-null pointer to the function.
 * @param data         Pointer to user-specified data.
 * @param data_deleter Pointer to a function that releases @p data or NULL.
 * @return 0 on success, negative value in case of an error
 */
int wget_register_finish_callback(wget_context_t *ctx,
                                  wget_callback_t *callback,
                                  void *data,
                                  wget_callback_data_deleter_t *data_deleter);

/**
 * Schedules download of given @p url in the background.
 *
 * Note: if you call this function when there is already an unfinished download
 * task going on then it'll return an error.
 *
 * @param url       URL to be downloaded.
 * @param path      Path where downloaded data shall be stored.
 *
 * @return 0 on success, negative value in case of an error
 */
int wget_background_download(wget_context_t *ctx,
                             const char *url,
                             const char *path);

#endif

