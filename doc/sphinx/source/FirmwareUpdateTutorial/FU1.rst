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

Firmware Update
===============

Introduction
^^^^^^^^^^^^

One of the most important applications of the LwM2M protocol is FOTA (**F**\
irmware update **O**\ ver **T**\ he **A**\ ir). After all, if you're having
a real deployment, you want to be able to keep it updated.

The firmware update procedure is well standardized within the LwM2M
Specification, and a standard Firmware Update Object (**/5**) can be used
to perform:

    - download,
    - verification,
    - upgrade.

At the same time, it is flexible enough so that a custom logic may be
introduced (e.g. differential updates). There are no restrictions on
verification method, image formats, and so on, thus they depend entirely on
the specific implementation.

Firmware update state machine and a general overview
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Let's have a quick glance at the Firmware Update State Machine as defined
by the LwM2M Specification:

.. image:: https://www.openmobilealliance.org/release/LightweightM2M/V1_1-20180710-A/HTML-Version/OMA-TS-LightweightM2M_Core-V1_1-20180710-A_files/firmware_update_mechanisms.svg
    :alt: LwM2M Firmware Update state machine

The texts over the transition edges are showing different events that may
happen either on the Client side or on the Server side.

You may notice that the transition diagram uses two separate entities, namely
**Res** and **State**. These correspond to **Update Result** and **State**
Resources in the Firmware Update Object.

- **Update Result** Resource keeps the result of the download or update attempts,
- **State** Resource keeps one of the four states as in the big boxes in the diagram above.

In any case, Anjay maintains these two automatically. In fact, the API of
Firmware Update module, which is about to be introduced, is simple enough, so
that an application developer can focus on implementing the I/O, verification
and the update process itself and let the library handle the LwM2M side of
the whole thing.

Moving forward, generally speaking, the whole **firmware update process
looks like this**:

#. Server initiates firmware download.
#. Client downloads the firmware and reports when it finished to the Server.
#. Server decides what to do next, and when ready, sends to the Client a request to perform the upgrade.
#. Client attempts to apply the firmware, and reports the status to the Server.

.. important::

    It shall be emphasized that **it is the LwM2M Server which initiates
    firmware download and upgrade**.

    `How to download firmware from a server?` seems to be a commonly asked
    question, but the LwM2M reality is: **one can't trigger this on a
    Client side in a standard way**. It's the Server which decides when it
    happens and either provides the client with an URI to perform the download,
    or directly sends the firmware to the Client.


.. _firmware-update-api:

API in Anjay
^^^^^^^^^^^^

Anjay comes with a built-in Firmware Update module, which simplifies FOTA
implementation for the user. At its core, Firmware Update module consists
of user-implemented callbacks of various sort. They are shown below, to
give an idea on what the implementation of FOTA would take:

.. highlight:: c
.. snippet-source:: include_public/anjay/fw_update.h

    typedef struct {
        /** Opens the stream that will be used to write the firmware package to;
         * @ref anjay_fw_update_stream_open_t */
        anjay_fw_update_stream_open_t *stream_open;
        /** Writes data to the download stream;
         * @ref anjay_fw_update_stream_write_t */
        anjay_fw_update_stream_write_t *stream_write;
        /** Closes the download stream and prepares the firmware package to be
         * flashed; @ref anjay_fw_update_stream_finish_t */
        anjay_fw_update_stream_finish_t *stream_finish;

        /** Resets the firmware update state and performs any applicable cleanup of
         * temporary storage if necessary; @ref anjay_fw_update_reset_t */
        anjay_fw_update_reset_t *reset;

        /** Returns the name of downloaded firmware package;
         * @ref anjay_fw_update_get_name_t */
        anjay_fw_update_get_name_t *get_name;
        /** Return the version of downloaded firmware package;
         * @ref anjay_fw_update_get_version_t */
        anjay_fw_update_get_version_t *get_version;

        /** Performs the actual upgrade with previously downloaded package;
         * @ref anjay_fw_update_perform_upgrade_t */
        anjay_fw_update_perform_upgrade_t *perform_upgrade;

        /** Queries security configuration that shall be used for an encrypted
         * connection; @ref anjay_fw_update_get_security_config_t */
        anjay_fw_update_get_security_config_t *get_security_config;

        /** Queries CoAP transmission parameters to be used during firmware
         * update. */
        anjay_fw_update_get_coap_tx_params_t *get_coap_tx_params;
    } anjay_fw_update_handlers_t;


Luckily, not all of them need to be implemented during initial
experiments. The mandatory ones are:

    - ``stream_open``,
    - ``stream_write``,
    - ``stream_finish``,
    - ``reset``,
    - ``perform_upgrade``.

Let's briefly discuss each one of them:

    - ``stream_open`` is called whenever a new firmware download is started
      by the Server. Its main responsibility is to prepare for receiving
      firmware chunks - e.g. by opening a file or getting flash storage
      ready, etc.

    - ``stream_write`` is called whenever there is a next firmware chunk
      received, ready to be stored. Its responsibility is to append the
      chunk to the storage.

    - ``stream_finish`` is called whenever the writing process finished and
      the stored data can now be thought of as a complete firmware image. It may
      be a good moment here to verify if the entire firmware image is valid.

    - ``reset`` is called whenever there was an error during firmware download,
      or if the Server decided to not pursue firmware update with downloaded
      firmware (e.g. because it was notified that firmware verification
      failed).

    - ``perform_upgrade`` is called whenever the download finished, the
      firmware is successfully verified on the Client and Server decided to
      upgrade the device.

In the next chapter we'll begin implementing all of these from scratch.
