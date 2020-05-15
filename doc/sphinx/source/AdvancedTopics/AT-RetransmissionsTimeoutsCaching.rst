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

Retransmissions, timeouts & response caching
============================================

Due to potential network instability, the need to retransmit a message
between a Client and a Server may sometimes occur. Detecting retransmissions
is especially important if the operation has some observable side effects.

Motivational examples
---------------------

Imagine a LwM2M Object with a numeric, executable Resource, whose value is
incremented every time a LwM2M Server performs Execute on it. Now, consider
the following scenario:

- the LwM2M Server performs an Execute on this specific Resource,
- the LwM2M Client receives the request, bumps the value, and sends a response,
- the response is lost due to unfortunate network conditions,
- the LwM2M Server attempts to increment the Resource again (sending exactly
  the same Execute request as before),
- the LwM2M Client receives the request.

LwM2M Client obtained both requests, and if it was unable to classify the
second one as a retransmission of the first, the resource would be incremented
twice, even though it would make much more sense to increment it just once.
On the other hand, caching the response, and detecting a retransmission,
would improve Client-Server communication integrity by preventing this
from happening.

Another scenario could be that the response is computationally expensive
(and time-consuming) to generate. In this case caching mechanism would
yield measurable performance benefits.

Caching mechanism
-----------------

Anjay provides a built-in message cache - when the request is received, Anjay
checks if there exists an appropriate response to it in the cache already. In
case there is one, it is retransmitted. Otherwise Anjay processes the request as
usual, in the end placing response in the cache for future use.

.. note::
    Cached response, matching a specific CoAP Request is identified by the
    following triplet:

     - CoAP Message Token,
     - CoAP Message ID,
     - Server endpoint name (host and port).

Every response in the cache sits there for at most ``MAX_TRANSMIT_SPAN``
as defined in `RFC7252 <https://tools.ietf.org/html/rfc7252>`_, in
`Section 4.8.2.  Time Values Derived from Transmission Parameters
<https://tools.ietf.org/html/rfc7252#section-4.8.2>`_, and after that time
it is automatically removed.

Cache size
----------

The size of the cache is specified at Anjay instantiation time by setting
``anjay_configuration_t::msg_cache_size`` to a non-zero value (zero disables
any caching). This limits the number of bytes used to store cached responses.

.. note::
    The cache size limit is global for all Servers - i.e. all responses,
    to all Servers are stored within a single cache.

Limitations
-----------

- If a response is too big to fit into the cache, it is **not cached**,
- If a response would fit into the cache, but the cache is currently full,
  responses (starting from the oldest) are **dropped** from the cache (even if
  they are still considered valid in terms of mentioned ``MAX_TRANSMIT_SPAN``),
  till the new response fits.


.. _coap-retransmission-parameters:

Configuring retransmissions and timeouts
----------------------------------------

Background
~~~~~~~~~~

To provide custom retransmission policy, affecting CoAP layer across
the library, one needs to set ``anjay_configuration_t::udp_tx_params``
accordingly prior library instantiation with ``anjay_new()``.

``anjay_configuration_t::udp_tx_params`` is a ``avs_coap_udp_tx_params_t``
structure, defined as follows:

.. code-block:: c

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


It should be noted that without any additional configuration,
Anjay uses default values as specified in the `Section 4.8 of RFC7252
<https://tools.ietf.org/html/rfc7252#section-4.8>`_:


+-----------------------+---------------+-----------------------------------------------------+
| Parameter             | Default value | Corresponding field in ``avs_coap_udp_tx_params_t`` |
+=======================+===============+=====================================================+
| ``ACK_TIMEOUT``       | 2 seconds     | ``ack_timeout``                                     |
+-----------------------+---------------+-----------------------------------------------------+
| ``ACK_RANDOM_FACTOR`` | 1.5           | ``ack_random_factor``                               |
+-----------------------+---------------+-----------------------------------------------------+
| ``MAX_RETRANSMIT``    | 4             | ``max_retransmit``                                  |
+-----------------------+---------------+-----------------------------------------------------+
| ``NSTART``            | 1             | ``nstart``                                          |
+-----------------------+---------------+-----------------------------------------------------+


Meaning of each parameter, calculations of timeouts and the number of retransmissions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``ACK_RANDOM_FACTOR``
^^^^^^^^^^^^^^^^^^^^^

Configures the amount of random perturbation to a timeout to a response to
an initial message (``ACK_TIMEOUT``, see next subsection). Its value has to
be at least ``1.0``. The randomness is mixed in as follows:

   * generate a random number ``r`` from a closed range ``[1.0, ACK_RANDOM_FACTOR]``,
   * multiply the ``ACK_TIMEOUT`` by ``r`` and use it as initial timeout.

.. admonition:: Example
   :class: hint

   Say the library has ``ACK_TIMEOUT`` set to `16s`.

   Now, if the ``ACK_RANDOM_FACTOR`` is ``1.0``, no random behavior is
   introduced, because the library is forced to pick a random number from
   a trivial interval ``[1.0, 1.0]``.

   However, if the ``ACK_RANDOM_FACTOR`` is, say, ``1.5``, the number picked
   may lie in range ``[1.0, 1.5]``, thus the actual time the library would wait
   may vary between ``[16, 24]`` seconds.


``ACK_TIMEOUT``
^^^^^^^^^^^^^^^

Configures the amount of time the library shall wait for the response to the
initial confirmable message (not retransmission).

.. admonition:: Example
   :class: hint

   Say the library wants to send a confirmable message.

   If ``ACK_TIMEOUT`` is set to, say, `10` seconds, the library sends the
   message and then waits ``10 * r`` seconds (``r`` is defined as in the
   above discussion about ``ACK_RANDOM_FACTOR``) for the initial response.


``MAX_RETRANSMIT``
^^^^^^^^^^^^^^^^^^

Configures the total number of retransmissions the library is allowed to
perform before giving up on message delivery.

.. admonition:: Example
   :class: hint

   If ``MAX_RETRANSMIT`` is set to, say, `4`, the library would send `1`
   initial message + up to `4` retransmissions, accounting for up to `5`
   messages in total.

   If ``MAX_RETRANSMIT`` is set to `0`, no retransmission would be attempted,
   and the library would give up if no response arrived after ``ACK_TIMEOUT *
   r`` seconds.


``NSTART``
^^^^^^^^^^

Configures the maximum number of exchanges that may be ongoing at the same time
with a given remote CoAP endpoint (i.e., a LwM2M Server).

In Anjay, it is mostly ignored. It is not recommended to set it to any other
value than the default of 1.

Higher values may be useful when writing applications using the low-level CoAP
APIs.

Exponential back-off
^^^^^^^^^^^^^^^^^^^^

After waiting for a response for ``t`` seconds , the wait time for the next
retransmission (in the absence of response) would be ``2 * t`` seconds. In
other words, retransmissions are performed with exponential back-off.

Example configuration
~~~~~~~~~~~~~~~~~~~~~

As an example, we may configure the library as follows:

.. code-block:: c

   avs_coap_udp_tx_params_t udp_tx_params = {
      // Wait at least 4 seconds for the initial response.
      .ack_timeout = avs_time_duration_from_scalar(4, AVS_TIME_S),
      // Do not randomize wait times for simplicity of the discussion,
      // thus "at least" in the comment above should be thought of as
      // "exactly".
      .ack_random_factor = 1.0,
      // Allow up to 4 retransmissions.
      .max_retransmit = 4,
      // leave the NSTART parameter at the default value of 1
      .nstart = 1
   };

   anjay_configuration_t configuration = {
      // Some other configuration ...
      .udp_tx_params = &udp_tx_params
   };

   // Create Anjay instance with custom transmission parameters
   anjay_t *anjay = anjay_new(&configuration);


The above configuration would result in the following retransmission times to a confirmable
message:

+----------+--------------+--------------------------------+----------------------------+
| Time [s] | Retry number | Wait time for the response [s] | Action by the library      |
+==========+==============+================================+============================+
| 0        | 0            | 4                              | send initial message       |
+----------+--------------+--------------------------------+----------------------------+
| 4        | 1            | 8                              | 1st retransmission         |
+----------+--------------+--------------------------------+----------------------------+
| 12       | 2            | 16                             | 2nd retransmission         |
+----------+--------------+--------------------------------+----------------------------+
| 28       | 3            | 32                             | 3rd retransmission         |
+----------+--------------+--------------------------------+----------------------------+
| 60       | 4            | 64                             | 4th (final) retransmission |
+----------+--------------+--------------------------------+----------------------------+
| 124      | --           | --                             | give up                    |
+----------+--------------+--------------------------------+----------------------------+

Other retransmission parameters
-------------------------------

While setting ``anjay_configuration_t::udp_tx_params`` parameter
covers most cases, there are also means to configure:

- DTLS handshake retransmissions
  (``anjay_configuration_t::udp_dtls_hs_tx_params`` `docs
  <../api/structanjay__configuration.html#ab8ca076537138e7d78bd1ee5d5e2031a>`__),

- firmware update module retransmissions (by implementing
  custom ``anjay_fw_update_get_coap_tx_params_t`` handler `docs
  <../api/fw__update_8h.html#a50900e2aaff21e91df693795965136b2>`__),

- in the commercial versions, there are also additional fields in
  ``anjay_configuration_t`` that configure transmission parameters for non-UDP
  transports.

We recommend to refer to the doxygen documentation for more details.
