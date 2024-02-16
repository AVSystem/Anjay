..
   Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Bootstrapper and SIM bootstrap
==============================

.. contents:: :local:

General description
-------------------

The LwM2M specification defines `Bootstrap from Smartcard
<http://www.openmobilealliance.org/release/LightweightM2M/V1_1_1-20190617-A/HTML-Version/OMA-TS-LightweightM2M_Core-V1_1_1-20190617-A.html#6-1-3-2-0-6132-Bootstrap-from-Smartcard>`_,
a mode of bootstrapping the device where the initial Bootstrap Information is
stored on a smart card - typically the SIM card in case of devices that use
cellular connectivity.

Standard file formats for this bootstrap information and related metadata are
defined in `Appendix G
<http://www.openmobilealliance.org/release/LightweightM2M/V1_1_1-20190617-A/HTML-Version/OMA-TS-LightweightM2M_Core-V1_1_1-20190617-A.html#15-0-Appendix-G-Storage-of-LwM2M-Bootstrap-Information-on-the-Smartcard-Normative>`_,
of the LwM2M Technical Specification, and specifications for the secure channel
between Smartcard and LwM2M Device Storage in `Appendix H
<http://www.openmobilealliance.org/release/LightweightM2M/V1_1_1-20190617-A/HTML-Version/OMA-TS-LightweightM2M_Core-V1_1_1-20190617-A.html#16-0-Appendix-H-Secure-channel-between-Smartcard-and-LwM2M-Device-Storage-for-secure-Bootstrap-Data-provisioning-Normative>`_
thereof.

The "bootstrapper" feature, available as a commercial extension to the Anjay
library, includes two modules that aid in implementing this part of the
specification:

* ``bootstrapper`` implements a parser for the file format described in
  `section G.3.4 of the Appendix G
  <http://www.openmobilealliance.org/release/LightweightM2M/V1_1_1-20190617-A/HTML-Version/OMA-TS-LightweightM2M_Core-V1_1_1-20190617-A.html#15-3-4-0-G34-EF-LwM2M_Bootstrap>`_
  mentioned above
* ``sim_bootstrap`` implements the flow of `ISO/IEC 7816-4
  <https://www.iso.org/obp/ui/#iso:std:iso-iec:7816:-4:ed-4:v1:en>`_ commands
  necessary to retrieve the aforementioned file

With the above features in place, all that's left to implement is actual
communication with the smart card, typically sending and receiving ``AT+CSIM``
commands to a cellular modem.

Bootstrapping from smart card has a number of advantages, including:

* Ability to store bootstrap information securely, increasing the device's
  resilience against tampering

* Possibility to remotely update bootstrap information using cellular
  infrastructure, without the need for a full firmware upgrade

* For devices controlled by cellular carriers - ability to control the bootstrap
  information without contacting the device manufacturer

Technical documentation
-----------------------

.. _cf-smart-card-bootstrap-enabling:

Enabling the bootstrapper module
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

If the bootstrapper feature is available in your version of Anjay, it can be
enabled at compile time by enabling the ``ANJAY_WITH_MODULE_BOOTSTRAPPER`` macro
in the ``anjay_config.h`` file or, if using CMake, enabling the corresponding
``WITH_MODULE_bootstrapper`` CMake option.

When this feature is enabled, the `anjay_bootstrapper()
<../api/bootstrapper_8h.html#a9763a2328433e93ae5121f0b218b43a1>`_ function can
be used. The user will need to provide an implementation of ``avs_stream_t``
that allows the Anjay code to read the file contained on the smartcard. The
``avs_stream_simple_input_create()`` function from the `avs_stream_simple_io.h
<https://github.com/AVSystem/avs_commons/blob/master/include_public/avsystem/commons/avs_stream_simple_io.h>`_
header is likely to be the easiest way to provide such an implementation, aside
from using the SIM bootstrap module described below.

Enabling and configuring the sim_bootstrap module
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Similarly, to enable the sim_bootstrap module, you can enable the
``ANJAY_WITH_MODULE_SIM_BOOTSTRAP`` macro in the ``anjay_config.h`` file or, if
using CMake, enable the corresponding ``WITH_MODULE_sim_bootstrap`` CMake
option. This requires that the bootstrapper feature is also enabled.

By default, the module will access the PKCS#15 application directory file and
search it for the EF(DODF-bootstrap) file in a way that is compliant with LwM2M
TS Appendix G mentioned above.

However, you can override the OID of the file to look for, by defining the
``ANJAY_MODULE_SIM_BOOTSTRAP_DATA_OID_OVERRIDE_HEX`` macro in ``anjay_config.h``
or setting the corresponding ``MODULE_sim_bootstrap_DATA_OID_OVERRIDE_HEX``
CMake option. It shall be set to a string containing hexlified DER
representation of the desired OID. The default, standards-compliant value is
``"672b0901"`` (which corresponds to OID 2.23.43.9.1), but you may need to
change it to a different value, for example some cards are known to use a
mistakenly encoded value of ``"0604672b0901"``.

Alternatively, you might define the
``ANJAY_MODULE_SIM_BOOTSTRAP_HARDCODED_FILE_ID`` macro (or set the
``MODULE_sim_bootstrap_HARDCODED_FILE_ID`` CMake option) to bypass the directory
search entirely and set a hardcoded file ID, e.g. ``0x6432``.

Once the module is enabled and configured, you can use the
`anjay_sim_bootstrap_stream_create()
<../api/sim__bootstrap_8h.html#a7cd497f30bfc7d36c6f0efb1db1d5a19>`_ function to
create an input stream suitable for passing to ``anjay_bootstrapper()``. In the
simplest case, you can also use the `anjay_sim_bootstrap_perform()
<../api/sim__bootstrap_8h.html#aa94114321f3af6532babde1efd9bdcec>`_ function
that combines both calls and automatically closes the stream as well.

Bootstrap information generator tool
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``generator.py`` application, located in the ``bootstrap`` directory of
Anjay source package, allows generating binary files in the EF LwM2M_Bootstrap
format that is supposed to be stored on smart cards, from a human-readable text
file format.

The ``generator.py`` script, by default, processes the standard input and
outputs to the standard output. However, an input file may be specified using
the `-c` option, and the output file may be specified using the ``-o`` option.

.. warning::

    The generator script is **NOT** intended to be safe to use with arbitrary
    input data. It is only intended for convenience when working with files
    created locally by trusted parties.

    **The input text files are evaluated as Python code**, and as such, running
    the generator script with untrusted input may lead to arbitrary operations
    being performed on the computer.

The input file shall specify a dictionary according to Python syntax where:

* On the top level, the keys shall be Object IDs, and the values shall be nested
  dictionaries describing the objects.

* On the Object level, the keys shall be Instance IDs, and the values shall be
  nested dictionaries describing the instances.

* On the Object Instance level, the keys shall be Resource IDs, and the values
  shall be either of:

  * Primitive types (numbers, booleans, strings or ``bytes`` objects) for
    single-instance resources

  * Lists of pairs (tuples of length 2) for Multiple-Instance Resources - in
    that case the first pair element shall be the Resource Instance ID, and the
    second one shall be the value of a primitive type

The constants from the ``OID`` and ``RID`` objects, as defined in the
`tests/integration/framework/test_utils.py
<https://github.com/AVSystem/Anjay/blob/master/tests/integration/framework/test_utils.py>`_
file, may be used to make the keys more descriptive, as in the example input
file (``bootstrap/configs/basic``):

.. highlight:: python
.. snippet-source:: bootstrap/configs/basic
    :commercial:

    {
        OID.Security: {
            1: {
                RID.Security.ServerURI          : 'coaps://eu.iot.avsystem.cloud:5684',
                RID.Security.Bootstrap          : False,
                RID.Security.Mode               : 0,  # PSK
                RID.Security.PKOrIdentity       : b'example-psk-identity',
                RID.Security.SecretKey          : b'3x@mpl3P5K53cr3tK3y',
                RID.Security.ShortServerID      : 1
            },
        },

        OID.Server: {
            1: {
                RID.Server.ShortServerID        : 1,
                RID.Server.Lifetime             : 86400,
                RID.Server.NotificationStoring  : False,
                RID.Server.Binding              : 'U'
            },
        }
    }

The above example is equivalent to the following data written only using
primitive values::

    {
        0: {
            1: {
                0: 'coaps://eu.iot.avsystem.cloud:5684',
                1: False,
                2: 0,
                3: b'example-psk-identity',
                5: b'3x@mpl3P5K53cr3tK3y',
                10: 1
            }
        },
        1: {
            1: {
                0: 1,
                1: 86400,
                6: False,
                7: 'U'
            }
        }
    }

.. highlight:: none

The following example shell session illustrates the way of generating the
binary bootstrap information file::

    ~/projects/anjay/bootstrap$ ./generator.py -c configs/basic -o basic_config.dat
    ~/projects/anjay/bootstrap$ hexdump -C basic_config.dat
    00000000  00 02 00 7a 00 00 00 00  5e 08 01 5b c8 00 22 63  |...z....^..[.."c|
    00000010  6f 61 70 73 3a 2f 2f 65  75 2e 69 6f 74 2e 61 76  |oaps://eu.iot.av|
    00000020  73 79 73 74 65 6d 2e 63  6c 6f 75 64 3a 35 36 38  |system.cloud:568|
    00000030  34 c1 01 00 c1 02 00 c8  03 14 65 78 61 6d 70 6c  |4.........exampl|
    00000040  65 2d 70 73 6b 2d 69 64  65 6e 74 69 74 79 c8 05  |e-psk-identity..|
    00000050  13 33 78 40 6d 70 6c 33  50 35 4b 35 33 63 72 33  |.3x@mpl3P5K53cr3|
    00000060  74 4b 33 79 c1 0a 01 00  01 00 00 12 08 01 0f c1  |tK3y............|
    00000070  00 01 c4 01 00 01 51 80  c1 06 00 c1 07 55        |......Q......U|
    0000007e


Example code
^^^^^^^^^^^^

.. note::

   The full code for the following example can be found in the
   ``examples/commercial-features/CF-SmartCardBootstrap`` directory in Anjay
   sources. Note that to compile and run it, you need to have access to a
   commercial version of Anjay that includes the bootstrapper feature.

The example is loosely based on the :doc:`../BasicClient/BC-MandatoryObjects`
tutorial, and additionally borrows much of the modem communication code from
:doc:`CF-NIDD`. Since the bootstrap information will be loaded from a smart
card, the ``setup_security_object()`` and ``setup_server_object()`` functions
are no longer necessary, and the calls to them can be replaced with direct calls
to `anjay_security_object_install()
<../api/security_8h.html#a5fffaeedfc5c2933e58ac1446fd0401d>`_ and
`anjay_server_object_install()
<../api/server_8h.html#a36a369c0d7d1b2ad42c898ac47b75765>`_:

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-SmartCardBootstrap/src/main.c
    :emphasize-lines: 22-23, 27-29

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

As you can see, the command line now expects a second argument with a name of
the file containing the bootstrap information.

This file is loaded using the ``bootstrap_from_sim()`` function, implemented as
follows:

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-SmartCardBootstrap/src/main.c

    typedef struct {
        avs_buffer_t *buffer;
    } fifo_t;

    // ...

    typedef struct {
        fifo_t fifo;
        int pts_fd;
    } modem_ctx_t;

    // ...

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

The ``sim_perform_command()`` function is a callback that is passed to the
``sim_bootstrap`` module logic, and performs the ``AT+CSIM`` command over a
serial port. The ``modem_getline()`` function it calls is almost identical to
the one originally implemented for :doc:`CF-NIDD`.

The ``bootstrap_from_sim()`` function itself is a wrapper over
`anjay_sim_bootstrap_perform()
<../api/sim__bootstrap_8h.html#aa94114321f3af6532babde1efd9bdcec>`_ that
additionally initializes and closes the card communication channel.

