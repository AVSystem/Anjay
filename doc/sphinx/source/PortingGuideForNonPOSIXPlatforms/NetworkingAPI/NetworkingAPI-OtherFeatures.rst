..
   Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under the AVSystem-5-clause License.
   See the attached LICENSE file for details.

Other features
==============

.. contents:: :local:

Introduction
------------

As you may have observed, the example network layer implementation from the
:doc:`previous tutorial <NetworkingAPI-IpStickiness>` is just over 400 lines in
length, while `the reference implementation
<https://github.com/AVSystem/avs_commons/tree/master/src/net/compat/posix>`_ is
over 2000 lines long. You may be wondering what do these thousands of lines
account for, if the shorter version already implements all features used by
Anjay.

This largely owes to the fact that the network layer in ``avs_commons`` has been
designed not just for Anjay, but for generic use in multiple projects. For
example, it is also used in `LibCWMP
<https://www.avsystem.com/products/libcwmp/>`_, another AVSystem product; it can
also be used to build third-party applications.

Most of the additional functionality that is not used by Anjay has been
developed in part or in full due to LibCWMP requirements.

This article will try to sum up the additional functionality that the reference
implementation provides on top of the :doc:`NetworkingAPI-IpStickiness` example.

avs_net_addrinfo support
------------------------

The reference implementation in ``avs_commons`` provides its own wrapper over
``getaddrinfo()`` - the ``avs_net_addrinfo_resolve()`` family of functions, that
is both used internally by the socket implementation, and might be used by user
code.

Additionally, this custom wrapper randomizes the list of addresses returned by
``getaddrinfo()``, which is a requirement of the CWMP protocol.

Additional operations
---------------------

The following operations, not present in the tutorial implementation, are added:

* ``send_to``
* ``receive_from``
* ``accept``, including a non-standard implementation for UDP
* ``get_interface_name``
* ``get_local_host``

Additionally, more options are supported for the ``get_opt`` operation:

* ``AVS_NET_SOCKET_OPT_ADDR_FAMILY``
* ``AVS_NET_SOCKET_OPT_MTU``

Socket configuration support
----------------------------

The reference implementation includes full support for the
`avs_net_socket_configuration_t
<https://github.com/AVSystem/avs_commons/blob/master/include_public/avsystem/commons/avs_socket.h#L92>`_
structure. No configuration options except ``reuse_addr`` and
``preferred_endpoint`` are directly used by Anjay, but they can be directly
specified by the user through the `socket_config field in anjay_configuration_t
<../../api/structanjay__configuration.html#a14968e097106889daad258f9e3a066d9>`_.

In the tutorial implementation all such configuration is ignored.

More polished implementation
----------------------------

Methods that exist in the tutorial implementation, are implemented in a more
polished way in the reference one. Some of such details have been already
mentioned in notes during the tutorial. These include:

* Proper handling of ``errno`` codes is included.
* Connect operation falls back to other IP addresses returned by
  ``getaddrinfo()`` in case of an error.
* Proper timeout handling is included for connect and send operations.
* Send operation is implemented using ``recvmsg()`` if possible, resulting in
  better handling of truncated datagrams.
* Send operation for TCP implements a loop to handle short writes properly.

Additional portability
----------------------

These tutorials have been written with readability in mind, designed only to
work on a typical Linux system. The reference implementation, on the other hand,
is designed to be highly portable and work not only on any POSIX-compliant
system, but also e.g. on `lwIP <https://www.nongnu.org/lwip/>`_ and
`Windows Sockets <https://docs.microsoft.com/windows/desktop/WinSock/windows-sockets-start-page-2>`_.

For this reason, the reference implementation includes multiple alternate
implementations for various functions, selected as needed at compile time but
contributing to the source code size:

* Support for IPv4 and IPv6 can be separately enabled or disabled at compile
  time.

  * Additional special handling of IPv4-mapped IPv6 addresses is provided for
    better interoperability.

* A custom implementation of ``inet_ntop()`` is provided for compatibility with
  platform that do not provide one.

* Timeout handling in "receive" and similar operations may be performed using
  either ``poll()`` or ``select()``, depending on which one is available.

* ``avs_net_resolved_endpoint_get_host_port()`` may use either ``getnameinfo()``
  or ``inet_ntop()``, depending on which one is available.

* Receive operation might use either ``recvmsg()`` or ``recvfrom()``, depending
  on which one is available.

* Network interface name handling might use either ``getifaddrs()`` or
  ``ioctl(SIOCGIFCONF)``, depending on which one is available.
