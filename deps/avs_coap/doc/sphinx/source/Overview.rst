..
   Copyright 2017-2021 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

Overview
========

Directory structure
-------------------

- ``src/``

  - ``async/`` - :ref:`async-api`
  - ``options/`` - :ref:`avs_coap_options_t-implementation`
  - ``oscore/`` (commercial version only)
  - ``streaming/`` - :ref:`streaming-api`
  - ``tcp/`` - :ref:`transport-specific-impls` (commercial version only)
  - ``udp/`` - :ref:`transport-specific-impls`


CoAP context
------------

All CoAP send/receive operations in the library require an ``avs_coap_ctx_t``
object. That object contains all the state required for parsing an incoming
packet or constructing an outgoing one. It can be used simultaneously as a
client and server, and abstracts away:

- BLOCK-wise transfers,
- Observations,
- Transport-specific parts of CoAP, like packet encoding/decoding, handling
  transport-specific messages.

The ``avs_coap_ctx_t`` can be thought of as an abstract class that has a set of
private pure virtual methods (``avs_coap_ctx_vtable_t``), and implements 4
public interfaces based on these vtable methods:

- CoAP asynchronous server,
- CoAP asynchronous client,
- CoAP streaming server,
- CoAP streaming client.

Asynchronous interfaces avoid blocking on socket operations whenever possible
and make heavy use of callbacks. Streaming interfaces do not return until
request handling is fully finished.

The CoAP context uses two *shared buffers* for message input and output. The
same set of buffers may be used in multiple CoAP context objects if the
application can guarantee that at most one CoAP context will perform IO
operations at any time.


.. _transport-specific-impls:

``avs_coap_ctx_vtable_t`` methods (e.g. ``src/tcp``, ``src/udp/``)
------------------------------------------------------------------

Pure virtual ``avs_coap_ctx_vtable_t`` represent operations that are
transport-specific. Their responsibilities include:

- serializing outgoing messages,
- parsing incoming messages,
- ensuring message delivery, by handling retransmissions and timeouts,
- matching sent requests to responses,
- internally handing all transport-specific details:

  - UDP:

    - message type
    - message ID
    - Separate Responses
    - CoAP Ping
    - Observe cancel via Reset response

  - TCP (commercial version only):

    - CSM and other 7.xx signaling messages


Async API (``src/async/``)
--------------------------

Asynchronous API makes heavy use of *exchanges*, represented by
``avs_coap_exchange_t`` struct. An exchange abstracts away a single
request-response pair, but here, unlike the virtual method layer, a "single
request-response pair" may include more than two packets if either request or
response uses CoAP BLOCK.

An exchange can be either:

- *client exchange*, if it represents a request made by ``avs_coap`` to
  a remote server and its response, or
- *server exchange*, if it represents a request made by a remote client that
  ``avs_coap`` is handling, and the generated response. Note that this includes
  Observe notifications, which are in essence repeated responses to previously
  received requests.

Both kinds of exchanges use the same ``avs_coap_exchange_t`` type. All client
exchanges are put onto ``avs_coap_base_t::client_exchanges`` list, and server
exchanges onto ``avs_coap_base_t::server_exchanges``.
``src/async/avs_coap_exchange.c`` contains logic common to both server and
client exchanges.

.. note::

    There should be two separate types for client and server exchanges, that
    share one base exchange type in a pattern similar to ``avs_coap_ctx_t`` and
    transport-specific contexts.

Every exchange is uniquely identified by a numeric identifier,
``avs_coap_exchange_id_t``, which allows the user to refer to a specific one
when required (e.g. when a single handler is shared between multiple concurrent
requests, or when the user wants to cancel handling of a specific request).

This layer is responsible for handling CoAP features that are independent from
the specific transport being used, including:

- BLOCK-wise transport handling,
- Observe establishment and cancellation.

Established observations are represented by ``avs_coap_observe_t`` objects,
which contain information necessary for handling (possibly block-wise) observes,
as well as user-defined "on observation canceled" handler. That type is also
used to ensure that Observe option values are strictly increasing, by setting it
to consecutive values starting from 0 (assigned to a response to the original
Observe request). From the user perspective, observations are identified using
``avs_coap_observe_id_t``, which is a CoAP token wrapped into a struct to make
it a separate type.

Having a valid ``avs_coap_observe_id_t`` allows the user to send a notification
associated with the observation. Every notification spawns an exchange object
that behaves as a repeated Read request.

.. note::

   Lifetime of spawned notifications is independent from ``avs_coap_observe_t``.
   In particular, if ``avs_coap`` starts sending a BLOCK-wise notification, and
   the remote client cancels the observation, the started notification can still
   be fully delivered.


.. _async-api:

Async API source files
~~~~~~~~~~~~~~~~~~~~~~

- ``src/async/avs_coap_exchange.c`` - logic common to both *client* and *server
  exchanges*:

  - read a chunk of user-provided payload (outgoing request/response),
  - add initial (``seq_num == 0``) BLOCK1/BLOCK2 option if necessary
    (message options do not contain BLOCK option and payload is larger than max
    block size),
  - pass message headers + payload to transport-specific layer.

- ``src/async/avs_coap_async_client.c`` - async client API implementation:

  - handle 2.31 Continue response to a request (repeat request with incremented
    BLOCK1 ``seq_num``),
  - send requests for further payload (repeat request with incremented BLOCK2
    ``seq_num``).

- ``src/async/avs_coap_async_server.c`` - async server API implementation:

  - match incoming requests to existing exchanges or create new ones
    (i.e. assemble multiple incoming BLOCK1 requests into one logical request),
  - detect timeouts of a BLOCK-wise request (when the remote endpoint stops
    sending messages related to an exchange),
  - handle Observe establishment or cancellation with Observe option,
  - implement API for sending async notifications.


Streaming API (``src/streaming/``)
----------------------------------

Streaming API builds upon the async one, providing an interface that uses
``avs_stream_t`` objects for passing payload data around instead of passing
buffers to user-provided callbacks. While this approach is a bit more convenient
to use, it comes with a cost: the API blocks for the whole time of transmitting
the request and following response. It is recommended to use the async API
instead if possible.


.. _streaming-api:

Streaming API source files
~~~~~~~~~~~~~~~~~~~~~~~~~~

- ``src/streaming/avs_coap_streaming_client.c`` - ``avs_stream_t``-compatible
  wrapper around ``async_client`` API:

  - loop until transfer of a whole client exchange is complete,
  - wrap request payload ``avs_coap_streaming_writer_t`` into
    ``avs_coap_payload_writer_t`` adapter used by ``async_client`` API,
  - expose received response payload as ``avs_stream_t``.

- ``src/streaming/avs_coap_streaming_server.c`` - ``avs_stream_t``-compatible
  wrapper around ``async_server`` API:

  - loop until transfer of a whole server exchange is complete,
  - expose received request payload as ``avs_stream_t``,
  - wrap response payload ``avs_coap_streaming_writer_t`` into
    ``avs_coap_payload_writer_t`` adapter used by ``async_server`` API.


CoAP context object and scheduler
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

CoAP context objects use the ``avs_sched_t`` object to handle any kinds of
timeouts. Because it is expected that the same scheduler object is also used for
other tasks, possibly also related to LwM2M, executing arbitrary tasks while
handling a streaming request might have disastrous consequences - for example,
handling two BLOCK-wise PUT/POST request concurrently.

For that reason, all CoAP context logic that depends on scheduler is
encapsulated in a single scheduler job
(``_avs_coap_retry_or_request_expired_job``). The streaming API then calls this
job directly, bypassing the scheduler.

That also means streaming API effectively prevents the scheduler from running,
and delays any scheduled tasks until after request handling is complete.


.. _avs_coap_options_t-implementation:

``avs_coap_options_t`` implementation
-------------------------------------

All code that operates on ``avs_coap_options_t``. This code should not touch
any "larger" structures of ``avs_coap`` (like CoAP context or exchange object).
