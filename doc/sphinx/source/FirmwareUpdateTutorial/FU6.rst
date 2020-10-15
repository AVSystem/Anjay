..
   Copyright 2017-2020 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

Download resumption
===================

Introduction
^^^^^^^^^^^^

Imagine that due to some bug or, for example, a power loss, the device
unexpectedly reboots during firmware download. Some portion of the firmware
could already have been written to a persistent memory, and it could be
a waste if that data was to be downloaded for the second time.

This is where the download resumption mechanism comes into play. If
this is a **PULL** mode download, the Server supports and uses `ETags
<https://en.wikipedia.org/wiki/HTTP_ETag>`_ and the firmware resource did
not expire (i.e. has the same ETag) there is a good chance the Client will
be able to resume a partially finished download.

Anjay and Firmware Update initial state
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Let's have a look at the ``anjay_fw_update_initial_state_t``:

.. highlight:: c
.. snippet-source:: include_public/anjay/fw_update.h
    :emphasize-lines: 9-51

    typedef struct {
        /**
         * Controls initialization of the State and Update Result resources. It is
         * intended to be used after a reboot caused by a firmware update attempt,
         * to report the update result.
         */
        anjay_fw_update_initial_result_t result;

        /**
         * Value to initialize the Package URI resource with. The passed string is
         * copied, so the pointer is allowed to become invalid after return from
         * @ref anjay_fw_update_install .
         *
         * Required when <c>result == ANJAY_FW_UPDATE_INITIAL_DOWNLOADING</c>; if it
         * is not provided (<c>NULL</c>) in such case, @ref anjay_fw_update_reset_t
         * handler will be called from @ref anjay_fw_update_install to reset the
         * Firmware Update object into the Idle state.
         *
         * Optional when <c>result == ANJAY_FW_UPDATE_INITIAL_DOWNLOADED</c>; in
         * this case it signals that the firmware was downloaded using the Pull
         * mechanism.
         *
         * In all other cases it is ignored.
         */
        const char *persisted_uri;

        /**
         * Number of bytes that has been already successfully downloaded and are
         * available at the time of calling @ref anjay_fw_update_install .
         *
         * It is ignored unless
         * <c>result == ANJAY_FW_UPDATE_INITIAL_DOWNLOADING</c>, in which case the
         * following call to @ref anjay_fw_update_stream_write_t shall append the
         * passed chunk of data at the offset set here. If resumption from the set
         * offset is impossible, the library will call @ref anjay_fw_update_reset_t
         * and @ref anjay_fw_update_stream_open_t to restart the download process.
         */
        size_t resume_offset;

        /**
         * ETag of the download process to resume. The passed value is copied, so
         * the pointer is allowed to become invalid after return from
         * @ref anjay_fw_update_install .
         *
         * Required when <c>result == ANJAY_FW_UPDATE_INITIAL_DOWNLOADING</c> and
         * <c>resume_offset > 0</c>; if it is not provided (<c>NULL</c>) in such
         * case, @ref anjay_fw_update_reset_t handler will be called from
         * @ref anjay_fw_update_install to reset the Firmware Update object into the
         * Idle state.
         */
        const struct anjay_etag *resume_etag;
    } anjay_fw_update_initial_state_t;


The highlighted fields can be used to arrange a download resumption. Recall
that we already passed this structure to ``anjay_fw_update_install`` in
previous chapters, but we've always made it zero-initialized before doing so.

.. note::

    **Quick remainder:** download resumption is supported for **PULL**
    mode downloads only.

Anyway, as you can see from the structure above, we're going to need three
pieces of information:

    - ``persisted_uri`` - that is, the URI from which the download was
      originally started,
    - ``resume_offset`` - which is just the number of bytes successfully
      stored before the device crashed / unexpectedly rebooted / whatever,
    - ``resume_tag`` - tag, allowing to validate that the Server still has
      the same firmware file available under given URI.

Implementation-wise, we'll start with introducing a structure that'd hold
the download state as well as utility functions that'd store and restore
the state from persistent storage:

.. highlight:: c
.. snippet-source:: examples/tutorial/firmware-update/download-resumption/src/firmware_update.c
    :emphasize-lines: 1, 10-116, 122-127

    #define _DEFAULT_SOURCE // for fileno()
    #include "./firmware_update.h"

    #include <assert.h>
    #include <errno.h>
    #include <stdio.h>
    #include <sys/stat.h>
    #include <unistd.h>

    typedef struct {
        char *persisted_uri;
        uint32_t resume_offset;
        anjay_etag_t *resume_etag;
    } download_state_t;

    static const char *FW_DOWNLOAD_STATE_NAME = "firmware_dl_state.bin";

    static int store_etag(FILE *fp, const anjay_etag_t *etag) {
        assert(etag);
        assert(etag->size > 0);
        if (fwrite(&etag->size, sizeof(etag->size), 1, fp) != 1
                || fwrite(etag->value, etag->size, 1, fp) != 1) {
            return -1;
        }
        return 0;
    }

    static int store_download_state(const download_state_t *state) {
        FILE *fp = fopen(FW_DOWNLOAD_STATE_NAME, "wb");
        if (!fp) {
            fprintf(stderr, "could not open %s for writing\n",
                    FW_DOWNLOAD_STATE_NAME);
            return -1;
        }
        const uint16_t uri_length = strlen(state->persisted_uri);
        int result = 0;
        if (fwrite(&uri_length, sizeof(uri_length), 1, fp) != 1
                || fwrite(state->persisted_uri, uri_length, 1, fp) != 1
                || fwrite(&state->resume_offset, sizeof(state->resume_offset), 1,
                          fp) != 1
                || store_etag(fp, state->resume_etag)) {
            fprintf(stderr, "could not write firmware download state\n");
            result = -1;
        }
        fclose(fp);
        if (result) {
            unlink(FW_DOWNLOAD_STATE_NAME);
        }
        return result;
    }

    static int restore_etag(FILE *fp, anjay_etag_t **out_etag) {
        assert(out_etag && !*out_etag); // make sure out_etag is zero-initialized
        uint8_t size;
        if (fread(&size, sizeof(size), 1, fp) != 1 || size == 0) {
            return -1;
        }
        anjay_etag_t *etag = anjay_etag_new(size);
        if (!etag) {
            return -1;
        }

        if (fread(etag->value, size, 1, fp) != 1) {
            avs_free(etag);
            return -1;
        }
        *out_etag = etag;
        return 0;
    }

    static int restore_download_state(download_state_t *out_state) {
        download_state_t data;
        memset(&data, 0, sizeof(data));

        FILE *fp = fopen(FW_DOWNLOAD_STATE_NAME, "rb");
        if (!fp) {
            fprintf(stderr, "could not open %s for reading\n",
                    FW_DOWNLOAD_STATE_NAME);
            return -1;
        }

        int result = 0;
        uint16_t uri_length;
        if (fread(&uri_length, sizeof(uri_length), 1, fp) != 1 || uri_length == 0) {
            result = -1;
        }
        if (!result) {
            data.persisted_uri = (char *) avs_calloc(1, uri_length + 1);
            if (!data.persisted_uri) {
                result = -1;
            }
        }
        if (!result
                && (fread(data.persisted_uri, uri_length, 1, fp) != 1
                    || fread(&data.resume_offset, sizeof(data.resume_offset), 1, fp)
                               != 1
                    || restore_etag(fp, &data.resume_etag))) {
            result = -1;
        }
        if (result) {
            fprintf(stderr, "could not restore download state from %s\n",
                    FW_DOWNLOAD_STATE_NAME);
            avs_free(data.persisted_uri);
        } else {
            *out_state = data;
        }
        fclose(fp);
        return result;
    }

    static void reset_download_state(download_state_t *state) {
        avs_free(state->persisted_uri);
        avs_free(state->resume_etag);
        memset(state, 0, sizeof(*state));
        unlink(FW_DOWNLOAD_STATE_NAME);
    }

    static struct fw_state_t {
        FILE *firmware_file;
        // anjay instance this firmware update singleton is associated with
        anjay_t *anjay;
        // Current state of the download. It is updated and persited on each
        // fw_stream_write() call.
        download_state_t download_state;
    } FW_STATE;

    static const char *FW_IMAGE_DOWNLOAD_NAME = "/tmp/firmware_image.bin";

In the next section, we'll discuss when state storing and restoring should
be done.

Persisting firmware state
^^^^^^^^^^^^^^^^^^^^^^^^^

When :ref:`we implemented <fw-download-io>` the ``fw_stream_open`` callback,
we ignored ``package_uri`` and ``package_etag``, because we didn't need it
at that time.

.. note::

    Persisting firmware state makes sense only if both ``package_uri``
    and ``package_etag`` are non-`NULL`. ``package_uri`` indicates it is
    a **PULL** mode transfer (the only mode supporting resumption), while
    ``package_etag`` allows the Client to verify that the downloaded file
    is the exactly the same as before the resumption happened -- without it
    there will be no resumption.

This time, however, we will save both of them in ``FW_STATE``. The only
missing piece is then the ``resume_offset``, which naturally can be updated
in ``fw_stream_write`` implementation after writing a chunk of data to the
storage. Of course, we also have to remember to reset the download state when
``fw_reset`` is called, as then the download is either failed or the Server
explicitly wants the Client to discard the firmware downloaded so far. These
ideas can be summarized as follows:

    - on a call to ``fw_stream_open`` we'll store ``package_uri`` and
      ``package_etag`` in ``FW_STATE``,
    - on a call to ``fw_stream_write`` we'll update the ``resume_offset``
      state and write the whole state information to persistent storage,
    - on a call to ``fw_reset`` we'll erase the download state.

.. important::

    The implementation of ``fw_stream_write`` as described above will be
    awkward on a UNIX-like systems. Complicated operating systems tend to
    have multiple layers of IO buffering, and it may take some time before
    the actual writes are made to the physical storage device. What it
    means for us is that we can't just call ``fwrite()`` and blindly update
    ``resume_offset`` with the number of bytes we ordered it to write even
    if it returned a success (because the data may still reside in some cache,
    maintained e.g. by the kernel).

    Because of that, rather than updating the download state file on
    each call to ``fw_stream_write``, it would be wiser to do it once in
    ``fw_stream_open``, and deduce the ``resume_offset`` from the size of
    the file.

    In an embedded application though, with no buffering (or without a concept
    of file), it's more appropriate to update ``resume_offset`` from within
    ``fw_stream_write`` instead, remembering to do so ONLY after having a
    high degree of certainty that the chunk of firmware was successfully
    written to the flash memory.

    Since we want to show the correct way of handling download resumption on
    embedded hardware while being relatively correct on non-embedded platforms,
    we'll use inefficient the ``fflush()`` and ``fsync()`` calls after each
    ``fwrite()`` which should flush the caches and trigger physical writes
    just to illustrate the point.

Keeping all these things in mind, let's start by refactoring ``fw_stream_open``
accordingly:

.. highlight:: c
.. snippet-source:: examples/tutorial/firmware-update/download-resumption/src/firmware_update.c

    static int fw_open_download_file(long seek_offset) {
        // It's worth ensuring we start with a NULL firmware_file. In the end
        // it would be our responsibility to manage this pointer, and we want
        // to make sure we never leak any memory.
        assert(FW_STATE.firmware_file == NULL);
        // We're about to create a firmware file for writing
        FW_STATE.firmware_file = fopen(FW_IMAGE_DOWNLOAD_NAME, "wb");
        if (!FW_STATE.firmware_file) {
            fprintf(stderr, "Could not open %s\n", FW_IMAGE_DOWNLOAD_NAME);
            return -1;
        }
        if (fseek(FW_STATE.firmware_file, seek_offset, SEEK_SET)) {
            fprintf(stderr, "Could not seek to %ld\n", seek_offset);
            fclose(FW_STATE.firmware_file);
            FW_STATE.firmware_file = NULL;
            return -1;
        }
        // We've succeeded
        return 0;
    }

    static int fw_stream_open(void *user_ptr,
                              const char *package_uri,
                              const struct anjay_etag *package_etag) {
        // We don't use user_ptr.
        (void) user_ptr;

        // We only persist firmware download state if we have both package_uri
        // and package_etag. Otherwise the download could not be resumed.
        if (package_uri && package_etag) {
            FW_STATE.download_state.persisted_uri = avs_strdup(package_uri);
            int result = 0;
            if (!FW_STATE.download_state.persisted_uri) {
                fprintf(stderr, "Could not duplicate package URI\n");
                result = -1;
            }
            anjay_etag_t *etag_copy = NULL;
            if (!result && package_etag) {
                etag_copy = anjay_etag_clone(package_etag);
                if (!etag_copy) {
                    fprintf(stderr, "Could not duplicate package ETag\n");
                    result = -1;
                }
            }
            if (!result) {
                FW_STATE.download_state.resume_etag = etag_copy;
            } else {
                reset_download_state(&FW_STATE.download_state);
                return result;
            }
        }

        return fw_open_download_file(0);
    }


Then, we can implement storing the download state logic in ``fw_stream_write``:

.. highlight:: c
.. snippet-source:: examples/tutorial/firmware-update/download-resumption/src/firmware_update.c
    :emphasize-lines: 3-7, 9-10, 14-22

    static int fw_stream_write(void *user_ptr, const void *data, size_t length) {
        (void) user_ptr;
        // NOTE: fflush() and fsync() are done to be relatively sure that
        // the data is passed to the hardware and so that we can update
        // resume_offset in the download state. They are suboptimal on UNIX-like
        // platforms, and are used just to illustrate when is the right time to
        // update resume_offset on embedded platforms.
        if (fwrite(data, length, 1, FW_STATE.firmware_file) != 1
                || fflush(FW_STATE.firmware_file)
                || fsync(fileno(FW_STATE.firmware_file))) {
            fprintf(stderr, "Writing to firmware image failed\n");
            return -1;
        }
        if (FW_STATE.download_state.persisted_uri) {
            FW_STATE.download_state.resume_offset += length;
            if (store_download_state(&FW_STATE.download_state)) {
                // If we returned -1 here, the download would be aborted, so it
                // is probably better to continue instead.
                fprintf(stderr,
                        "Could not store firmware download state - ignoring\n");
            }
        }
        return 0;
    }

The next step is to make sure that ``fw_reset`` resets the download state as well:

.. highlight:: c
.. snippet-source:: examples/tutorial/firmware-update/download-resumption/src/firmware_update.c
    :emphasize-lines: 11-12

    static void fw_reset(void *user_ptr) {
        // Reset can be issued even if the download never started.
        if (FW_STATE.firmware_file) {
            // We ignore the result code of fclose(), as fw_reset() can't fail.
            (void) fclose(FW_STATE.firmware_file);
            // and reset our global state to initial value.
            FW_STATE.firmware_file = NULL;
        }
        // Finally, let's remove any downloaded payload
        unlink(FW_IMAGE_DOWNLOAD_NAME);
        // And reset any download state.
        reset_download_state(&FW_STATE.download_state);
    }

And the last piece of the implementation would be to read the download state (if any)
at initialization stage, and before installing the firmware update module in Anjay:

.. highlight:: c
.. snippet-source:: examples/tutorial/firmware-update/download-resumption/src/firmware_update.c
    :emphasize-lines: 9-23

    int fw_update_install(anjay_t *anjay) {
        anjay_fw_update_initial_state_t state;
        memset(&state, 0, sizeof(state));

        if (access(FW_UPDATED_MARKER, F_OK) != -1) {
            // marker file exists, it means firmware update succeded!
            state.result = ANJAY_FW_UPDATE_INITIAL_SUCCESS;
            unlink(FW_UPDATED_MARKER);
            // we can get rid of any download state if the update succeeded
            reset_download_state(&FW_STATE.download_state);
        } else if (!restore_download_state(&FW_STATE.download_state)) {
            // download state restored, it means we can try using download
            // resumption
            if (fw_open_download_file(state.resume_offset)) {
                // the file cannot be opened or seeking failed
                reset_download_state(&FW_STATE.download_state);
            } else {
                state.persisted_uri = FW_STATE.download_state.persisted_uri;
                state.resume_offset = FW_STATE.download_state.resume_offset;
                state.resume_etag = FW_STATE.download_state.resume_etag;
                state.result = ANJAY_FW_UPDATE_INITIAL_DOWNLOADING;
            }
        }
        // make sure this module is installed for single Anjay instance only
        assert(FW_STATE.anjay == NULL);
        FW_STATE.anjay = anjay;
        // install the module, pass handlers that we implemented and initial state
        // that we discovered upon startup
        return anjay_fw_update_install(anjay, &HANDLERS, NULL, &state);
    }
