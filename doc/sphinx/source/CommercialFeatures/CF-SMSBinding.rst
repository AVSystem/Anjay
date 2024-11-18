..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

SMS Binding
===========

.. contents:: :local:

General description
-------------------

One of LwM2M's advantages is the possibility to choose from numerous underlying
protocol stacks. Although most applications use CoAP over UDP,
`LwM2M TS: Transport Bindings` document specifies how LwM2M messages can be
conveyed over other protocols, like HTTP, MQTT, TCP, NIDD, or SMS. Such a wide
choice increases flexibility of LwM2M, making it the right choice in a number of
different applications.

Anjay's **SMS Binding** feature incorporates a feature-complete implementation
of SMS binding as specified in the LwM2M specification. Thanks to that, you can:

* use SMS as a sole transportation method to be able to deploy LwM2M devices in
  areas where connectivity over IP is unavailable or economically unjustified,
* integrate Anjay with already existing devices which are not capable of
  internet communication,
* use SMS together with UDP, either as an alternative binding to increase the
  reliability of the connection, or save costs and battery usage by using SMS
  triggers to wake up the device and then bring on the connection over UDP.

The implementation is also **interoperable with DTLS** and supports
**Concatenated SMS (CSMS)**, both for the inbound and outbound messages, in case
the modem used doesn't have support for that. With CSMS enabled it's possible to
send payloads up to 34170 bytes, without any fragmentation on CoAP level.

**SMS Binding** feature also provides a couple of utilities for developers:

* sample implementation of an SMS driver, which uses the standard AT command set
  to send, receive and manage SMS messages and communicates over a serial port
  with **any standards-compliant cellular modem**,
* utility Python script, which **simulates an AT modem** and acts as a proxy -
  all messages are conveyed further over UDP to the target server.

Technical documentation
-----------------------

Enabling SMS binding support
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

If **SMS Binding** feature is available in your version of Anjay, the support
for SMS transport can be enabled by either defining ``ANJAY_WITH_SMS`` in
``anjay_config.h`` or, if using CMake, enabling ``WITH_SMS`` option.

You may also want to enable the ``ANJAY_WITH_SMS_MULTIPART`` macro, which
enables support for Concatenated SMS, both for incoming and outgoing traffic
(for outbound traffic it can also be configured in runtime). Using Concatenated
SMS raises the MTU reported to upper layers, which may solve issues with some
types of CoAP or DTLS messages that can be larger than 140 bytes. This setting
should be also disabled for modems, which are capable of handling CSMS on their
own.

Anjay, similarly to how network sockets are implemented, uses an abstraction
layer for SMS connections to remain hardware agnostic. It's the developers'
responsibility to implement a driver which handles specific hardware, although
the library comes with a basic reference implementation for AT modems, which
communicates with them over a serial port. To compile it in, please define
``ANJAY_WITH_MODULE_AT_SMS`` or enable ``WITH_MODULE_at_sms`` CMake option.

Usage example
^^^^^^^^^^^^^

.. important::

   For simplicity, we'll also use the example SMS driver implementation for AT
   modems through the whole tutorial. You can find more information about
   features and limitations of that implementation in
   `driver's documentation <../api/at__sms_8h.html>`_.

   To make those examples work out-of-the-box with the virtual modem/SMS proxy
   script, client's and server's phone numbers will be set to the default values
   included in the script.

Simple, unsecured connection
""""""""""""""""""""""""""""

.. note::

   The full code for the following example can be found in the
   ``examples/commercial-features/CF-SMS`` directory in Anjay sources. Note that
   to compile and run it, you need to have access to a commercial version of
   Anjay that includes the SMS binding feature.

As an example, we'll modify the code from the
:doc:`../BasicClient/BC-MandatoryObjects` tutorial.

To connect to the server over SMS, we have to make a couple of little changes
to the original application.

Since the server is reachable by phone number, not by an internet address, we
have to reconfigure the LwM2M Security object - server URI should be changed
and the security mode for the SMS binding should be specified.

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-SMS/src/main.c
   :emphasize-lines: 8, 10

    static int setup_security_object(anjay_t *anjay) {
        if (anjay_security_object_install(anjay)) {
            return -1;
        }

        const anjay_security_instance_t security_instance = {
            .ssid = 1,
            .server_uri = "tel:+12125550178",
            .security_mode = ANJAY_SECURITY_NOSEC,
            .sms_security_mode = ANJAY_SMS_SECURITY_NOSEC
        };

        // Anjay will assign Instance ID automatically
        anjay_iid_t security_instance_id = ANJAY_ID_INVALID;
        if (anjay_security_object_add_instance(anjay, &security_instance,
                                               &security_instance_id)) {
            return -1;
        }

        return 0;
    }

Next up, we should update the preferred binding information in the configuration
of LwM2M Server object.

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-SMS/src/main.c
   :emphasize-lines: 17, 18

    static int setup_server_object(anjay_t *anjay) {
        if (anjay_server_object_install(anjay)) {
            return -1;
        }

        const anjay_server_instance_t server_instance = {
            // Server Short ID
            .ssid = 1,
            // Client will send Update message often than every 60 seconds
            .lifetime = 60,
            // Disable Default Minimum Period resource
            .default_min_period = -1,
            // Disable Default Maximum Period resource
            .default_max_period = -1,
            // Disable Disable Timeout resource
            .disable_timeout = -1,
            // Sets preferred transport to SMS
            .binding = "S"
        };

        // Anjay will assign Instance ID automatically
        anjay_iid_t server_instance_id = ANJAY_ID_INVALID;
        if (anjay_server_object_add_instance(anjay, &server_instance,
                                             &server_instance_id)) {
            return -1;
        }

        return 0;
    }

As the example SMS driver expects a path to a file representing an AT terminal,
we'll accept the path as an application's argument and instantiate the driver.
Client's phone number is a part of the registration message, thus it must be
included in the config structure.

.. note::

   Technically speaking, the library expects a MSISDN, which is a country
   code-prefixed phone number without the '+' sign or other prefixes specific
   to your location.

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-SMS/src/main.c
   :emphasize-lines: 2-4, 13-14

    int main(int argc, char *argv[]) {
        if (argc != 3) {
            avs_log(tutorial, ERROR, "usage: %s ENDPOINT_NAME MODEM_DEVICE",
                    argv[0]);
            return -1;
        }

        const anjay_configuration_t CONFIG = {
            .endpoint_name = argv[1],
            .in_buffer_size = 4000,
            .out_buffer_size = 4000,
            .msg_cache_size = 4000,
            .sms_driver = anjay_at_sms_create(argv[2]),
            .local_msisdn = "14155550125"
        };

        anjay_t *anjay = anjay_new(&CONFIG);
        if (!anjay) {
            avs_log(tutorial, ERROR, "Could not create Anjay object");
            return -1;
        }

        int result = 0;
        // Setup necessary objects
        if (setup_security_object(anjay) || setup_server_object(anjay)) {
            result = -1;
        }

        if (!result) {
            result = anjay_event_loop_run(
                    anjay, avs_time_duration_from_scalar(1, AVS_TIME_S));
        }

        anjay_delete(anjay);
        return result;
    }

If you have access to an AT modem with SMS functionality and a LwM2M server
with SMS gateway configured, you need to think about a couple of matters:

* the example AT driver expects the device to not echo the commands sent, that
  is ``ATE0`` should be either sent to the modem first, or configured earlier,
* the driver communicates with the modem using plain ``read()`` / ``write()``
  routines and is not aware of the characteristics of the terminal, so it should
  be configured first. For example, to make this application work with Quectel
  UG96 modem, on Linux, over 115200-8-N-1 serial connection, following command
  had to be issued:
  ``stty -F /dev/ttyACM0 115200 -cs8 -cstopb -parenb -icrnl``.

Otherwise, to run the example, you can use the ``vmodem.py`` script instead,
which acts as a virtual AT modem, translating SMS messages to UDP packets and
vice-versa. You can find it in ``tests/integration/framework/sms`` directory.

Example run follows:

.. highlight:: none

::

    $ tests/integration/framework/sms/vmodem.py --host eu.iot.avsystem.cloud --port 5683
    2022-03-03 12:41:49 user root[19173] INFO Modem PTY: /dev/pts/5

The scripts informs us that it has opened a virtual terminal at ``/dev/pts/5``.
Now you can build the application by executing
``make commercial_feature_examples`` and run it with
``output/bin/examples/anjay-sms endpoint_name /dev/pts/5``.

DTLS over SMS
"""""""""""""

.. note::

   The full code for the following example can be found in the
   ``examples/commercial-features/CF-SMS-PSK`` directory in Anjay sources. Note
   that to compile and run it, you need to have access to a commercial version
   of Anjay that includes the SMS binding feature.

To secure the connection using DTLS over SMS, we'll introduce similar changes
to those described in :doc:`../BasicClient/BC-Security` tutorial, but targeting
fields explicitly related to the SMS binding.

.. note::

   LwM2M allows only for PSK mode to be used with DTLS over SMS.

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-SMS-PSK/src/main.c
   :emphasize-lines: 6-7, 13-18

    static int setup_security_object(anjay_t *anjay) {
        if (anjay_security_object_install(anjay)) {
            return -1;
        }

        static const char PSK_IDENTITY[] = "identity";
        static const char PSK_KEY[] = "P4s$w0rd";

        const anjay_security_instance_t security_instance = {
            .ssid = 1,
            .server_uri = "tel:+12125550178",
            .security_mode = ANJAY_SECURITY_NOSEC,
            .sms_security_mode = ANJAY_SMS_SECURITY_DTLS_PSK,
            .sms_key_parameters = (const uint8_t *) PSK_IDENTITY,
            .sms_key_parameters_size = strlen(PSK_IDENTITY),
            .sms_secret_key = (const uint8_t *) PSK_KEY,
            .sms_secret_key_size = strlen(PSK_KEY),
            .server_name_indication = "eu.iot.avsystem.cloud"
        };

        // Anjay will assign Instance ID automatically
        anjay_iid_t security_instance_id = ANJAY_ID_INVALID;
        if (anjay_security_object_add_instance(anjay, &security_instance,
                                               &security_instance_id)) {
            return -1;
        }

        return 0;
    }

Notice that the ``security_mode`` setting must remain untouched as it's
unused by the application, but LwM2M Security object specification requires
its presence.

.. important::

   When using DTLS, Anjay by default sets the Server Name Indication (SNI)
   extension field to the address extracted from ``server_uri`` field, which
   in our case is the server's gateway phone number. As in this example we're
   using the virtual modem script to make the example more accessible, which
   also means that in fact UDP is used to connect to the server, we have to
   override the SNI by assigning ``server_name_indication`` field to our target
   server's address. Otherwise, the server will attempt to recognize the phone
   number as correct server name.

To make DTLS over SMS work correctly, we have to ensure that appropriate
ciphersuite is used. The MTU of SMS is just 140 bytes, so some ciphersuites
have too much overhead to be conveyed over SMS messages.

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-SMS-PSK/src/main.c
   :emphasize-lines: 8-12

    const anjay_configuration_t CONFIG = {
        .endpoint_name = argv[1],
        .in_buffer_size = 4000,
        .out_buffer_size = 4000,
        .msg_cache_size = 4000,
        .sms_driver = anjay_at_sms_create(argv[2]),
        .local_msisdn = "14155550125",
        .default_tls_ciphersuites = {
            // TLS_PSK_WITH_AES_128_CCM_8
            .ids = (uint32_t[]){ 0xC0A8 },
            .num_ids = 1
        }
    };

.. note::
   ``TLS_PSK_WITH_AES_128_CCM_8`` is one of LwM2M's recommended ciphersuites to
   be used with SMS bindings. The ID assignments of ciphersuites are maintained
   by IANA and can be found in `this document
   <https://www.iana.org/assignments/tls-parameters/tls-parameters.xhtml#tls-parameters-4>`_.

Alternatively, one can use Concatenated SMS messages instead, which have
logical MTU large enough to work with all types of ciphersuites, but it means
that most of the messages sent to the server, even small ones, will be
fragmented. To enable multipart SMS messages, set ``prefer_multipart_sms``
in the ``CONFIG`` structure above to ``true``.

.. _anjay-sms-trigger-mode:

SMS Trigger Mode
""""""""""""""""

.. note::

   The full code for the following example can be found in the
   ``examples/commercial-features/CF-SMS-UDP`` directory in Anjay sources. Note
   that to compile and run it, you need to have access to a commercial version
   of Anjay that includes the SMS binding feature.

LwM2M 1.1 introduces Trigger Mode, which provides the possibility to request a
LwM2M Update via SMS (by executing Registration Update Trigger resource) and
receive the response by currently used transport binding. Similar behavior can
be achieved using *US* or *UQS* binding modes available in LwM2M 1.0.

Following example will build upon the code from `Simple, unsecured connection`
section, use LwM2M 1.1 and UDP as transport binding.

Firstly, let's change the Server URI to use UDP again. Server's phone number
will be passed through another field, ``server_sms_number``, in MSISDN form.

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-SMS-UDP/src/main.c
   :emphasize-lines: 3, 6

    const anjay_security_instance_t security_instance = {
        .ssid = 1,
        .server_uri = "coap://eu.iot.avsystem.cloud:5683",
        .security_mode = ANJAY_SECURITY_NOSEC,
        .sms_security_mode = ANJAY_SMS_SECURITY_NOSEC,
        .server_sms_number = "12125550178"
    };

To change the binding back to UDP and enable the Trigger resource in Server
object which controls Trigger Mode, apply following changes:

.. highlight:: c
.. snippet-source:: examples/commercial-features/CF-SMS-UDP/src/main.c
   :emphasize-lines: 12-15

    const anjay_server_instance_t server_instance = {
        // Server Short ID
        .ssid = 1,
        // Client will send Update message often than every 60 seconds
        .lifetime = 60,
        // Disable Default Minimum Period resource
        .default_min_period = -1,
        // Disable Default Maximum Period resource
        .default_max_period = -1,
        // Disable Disable Timeout resource
        .disable_timeout = -1,
        // Sets preferred transport to UDP
        .binding = "U",
        // Enables optional Trigger resource and sets it to true
        .trigger = &(const bool) { true }
    };

