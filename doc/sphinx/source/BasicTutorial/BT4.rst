..
   Copyright 2017 AVSystem <avsystem@avsystem.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

Few notes on general usage
==========================

Pros and cons of being single threaded
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To start, we first need to establish that Anjay is a single-threaded
library. It relies on the user calling ``anjay_serve()`` whenever some
packet is receieved and ``anjay_sched_run()`` regularly, as described in the
:doc:`previous chapter <BT3>`. Both of the methods block until the incoming
message or (respectively) a job is processed.

.. note::

    The incoming message may be split between few packets (e.g. in case of
    blockwise transfers), however ``anjay_serve()`` is called just for the
    first one. Other parts (if any) are fetched internally by Anjay. In other
    words: ``anjay_serve()`` blocks until the message gets handled completely.

Because ``anjay_serve()`` blocks after a packet arrives, the library can
handle at most one LwM2M Server at time, which makes its usage convenient,
as one does not have to worry about :ref:`data model <data-model>`
being accessed or modified by multiple LwM2M Servers at the same time.
Unfortunately it may happen to be a problem, as during blockwise transfers
the library is unable to respond to other LwM2M Servers with anything else
than 5.03 Service Unavailable.

Before getting worried about it too much, one shall realize that the above
behavior happens only when a blockwise transfer is issued on some part of
the data model - i.e. for that to become a problem one would have to store
and transfer big amounts of data regularly through LwM2M which, in context of
resource constrained environments targeted by the LwM2M protocol might not
be the best fit.

We believe that pretty much all transfers of bigger amounts of data happen
due to firmware update of the Client. In this case one may mitigate described
problem quite easily by either spawning a child process which would download
the firmware in the background, or by using our :ref:`asynchronous CoAP
downloader <coap-pull-download>`. Both methods will leave the library in
an operational state during the transfer.

Transactions and ``anjay_serve()``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Our data model supports transactional operations. They are here to ensure that
whenever something goes wrong during a transaction, all changes applied since
its beginning can be reverted - keeping the LwM2M Client in a consistent state.

As we already know, calling ``anjay_serve()`` corresponds to processing a
single LwM2M request. This, along with properly implemented transaction
handlers guarantees that if the LwM2M Client was in a consistent state
before request had been received, then it will remain in a consistent state
after the request is processed. Moreover, because of single-threaded mode of
operation no other LwM2M Server can see the LwM2M Client being in partially
consistent state.

Things work a bit different during the Bootstrap Sequence though. When the
Client/Server initiated Bootstrap begins, Anjay fires transaction handlers
for all data model entities. At the same time, it enters the state where
requests originated from Bootstrap Server only are handled - there may
be more than one such request, and so ``anjay_serve()`` could get called
multiple times. This again does not hurt consistency in any way, because
according to the LwM2M Specification, the LwM2M Client may ignore other
servers during that special time, and Anjay is doing just that - meaning
that they won't be able to observe intermediate initialization state.

After the Bootstrap Sequence finishes Anjay checks that the data model is
valid, and if it isn't the previous correct state will be restored, which
proves the point.
