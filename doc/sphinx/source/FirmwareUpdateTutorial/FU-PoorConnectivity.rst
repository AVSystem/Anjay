..
   Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Poor network connectivity
=========================

Introduction
^^^^^^^^^^^^

It is sometimes the case that the network connectivity is unstable,
and during the download some packets get lost. Depending on the underlying
download protocol different failures are handled differently.


The definition of "download failure"
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Since the variety of protocols may be used to perform the transfer, we
need to understand better what conditions cause the download to fail. Let's
consider this case by case:

CoAP(s)/UDP
"""""""""""

The download fails if either the connection could not be established (e.g.
DTLS handshake failure) or when all request retransmissions were performed
and no response have been received.

CoAP(s)/TCP
"""""""""""

The download fails if either the connection could not be established (e.g.
TLS handshake failure, remote host is down) or the TCP stack declares
the connection as broken, or when there is no response to the request for
the configured time; by default that time is 30 seconds.

HTTP(s)
"""""""

The download fails due to similar reasons as above. Since HTTP(s) operates
over TCP and the TCP stack maintains retransmissions and other things in a
way outside of our control. The reponse timeout is configured the same way as
for CoAP(s)/TCP, and the default timeout is also 30 seconds.


So what happens when the download fails?
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

When the download fails, the firmware update module calls ``reset``
handler. Its implementation is required to cleanup any outstanding resources,
and prepare the Client for a potential new download request.

Unfortunately it also means that the firmware image downloaded so far
needs to be deleted. In the current implementation there is no support for
continuation of firmware download which failed due to network connectivity
problems as it doesn't seem to be supported by the LwM2M protocol.


How can we ensure higher success rate?
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

For CoAP/UDP, you can provide the :ref:`CoAP transmission parameters
<coap-retransmission-parameters>` by implementing the ``get_coap_tx_params``
handler, which is a part of ``anjay_fw_update_handlers_t``. If not provided,
default CoAP transmission parameters (or passed as part of
``anjay_configuration_t``) will be used.

In a similar manner, for TCP-based transports (i.e., CoAP(s)/TCP and HTTP(s))
you can implement the ``get_tcp_request_timeout`` to set a custom request
timeout. This is the time of stream inactivity that will be treated as an error.
