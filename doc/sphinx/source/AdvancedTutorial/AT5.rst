..
   Copyright 2017-2019 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

Retransmissions & response caching
==================================

Due to potential network instability, the need to retransmit a message
between a Client and a Server may sometimes occur. Detecting retransmissions
is especially important if the operation has some observable side effects.

Motivational examples
---------------------

Imagine an LwM2M Object with a numeric, executable Resource, whose value is
incremented every time an LwM2M Server performs Execute on it. Now, consider
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

Anjay provides a built-in message cache (as an optional feature), that can be
(and is, by default) enabled at a compile time (via
``WITH_AVS_COAP_MESSAGE_CACHE`` CMake option).

When the request is received, Anjay checks if there exists an appropriate
response to it in the cache already. In case there is one, it is
retransmitted. Otherwise Anjay processes the request as usual, in the end
placing response in the cache for future use.

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
