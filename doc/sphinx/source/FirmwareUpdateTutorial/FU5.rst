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
**5 minutes**.

HTTP(s)
"""""""

The download fails due to similar reasons as above. Since HTTP(s) operates
over TCP and the TCP stack maintains retransmissions and other things in a
way outside of our control. The difference is that here the response timeout
is fixed to **30 seconds**.


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

As described in previous sections, for any TCP based transport you can't do
much in terms of when the firmware download is considered as failed. The
timeouts are, at the time of writing this tutorial, fixed and cannot be
changed during runtime.

However, CoAP/UDP provides much more control. :ref:`CoAP transmission
parameters <coap-retransmission-parameters>` can be provided by
the user, who implements ``get_coap_tx_params``, which is a part of
``anjay_fw_update_handlers_t``. If not provided, default CoAP transmission
parameters (or passed as part of ``anjay_configuration_t``) will be used.
