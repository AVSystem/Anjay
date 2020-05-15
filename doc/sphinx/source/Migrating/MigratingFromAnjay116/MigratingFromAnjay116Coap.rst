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

Replacement of CoAP implementation
==================================

.. highlight:: c

Anjay 2.x uses a completely new CoAP implementation called ``avs_coap``. The old
CoAP component of ``avs_commons`` has been removed in ``avs_commons`` 4.1 that
Anjay 2.3 uses. If your code used the raw CoAP APIs of that component, you will
need to migrate to either the new ``avs_coap`` library or an entirely different
CoAP implementation.

.. note::

    The new ``avs_coap`` library has a higher-level API, designed to abstract
    away the differences between e.g. UDP and TCP transports. Some of the
    functionality of the legacy library, especially that related to parsing,
    serializing, sending and receiving raw, isolated messages (as opposed to
    proper, conformant CoAP exchanges), is not provided in the public API for
    this reason.

UDP transmission parameters type
--------------------------------

In Anjay configuration, whenever the CoAP/UDP transmission parameters has to be
specified, the new ``avs_coap_udp_tx_params_t`` type replaces the
``avs_coap_tx_params_t`` type from the old CoAP implementation.

* **Old API:**
  ::

      /** CoAP transmission params object. */
      typedef struct {
          /** RFC 7252: ACK_TIMEOUT */
          avs_time_duration_t ack_timeout;
          /** RFC 7252: ACK_RANDOM_FACTOR */
          double ack_random_factor;
          /** RFC 7252: MAX_RETRANSMIT */
          unsigned max_retransmit;
      } avs_coap_tx_params_t;

* **New API:**

  .. snippet-source:: deps/avs_coap/include_public/avsystem/coap/udp.h

      /** CoAP transmission params object. */
      typedef struct {
          /** RFC 7252: ACK_TIMEOUT */
          avs_time_duration_t ack_timeout;
          /** RFC 7252: ACK_RANDOM_FACTOR */
          double ack_random_factor;
          /** RFC 7252: MAX_RETRANSMIT */
          unsigned max_retransmit;
          /** RFC 7252: NSTART */
          size_t nstart;
      } avs_coap_udp_tx_params_t;

.. note::

   While the ``ack_timeout``, ``ack_random_factor`` and ``max_retransmit``
   fields have equivalent semantics, **the new** ``nstart`` **field has a
   default value of** ``1``, **and it is not allowed to leave it as** ``0``.

.. warning::

   Support for ``nstart`` values other than ``1`` in Anjay and ``avs_coap`` is
   considered experimental.
