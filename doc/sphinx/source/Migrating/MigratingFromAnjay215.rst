..
   Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
   AVSystem Anjay LwM2M SDK
   All rights reserved.

   Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
   See the attached LICENSE file for details.

Migrating from Anjay 2.15.x
===========================

.. contents:: :local:

.. highlight:: c

Introduction
------------

While most changes since Anjay 2.15 are minor, some of them (changes to commonly
used APIs such as Attribute Storage and offline mode control) are breaking.
There is a change to the way the ``con`` attribute is handled in the API.
Additionally, the upgrade to ``avs_commons`` 5.0 includes refactoring of the
APIs related to (D)TLS PSK credentials.


Changes in Anjay proper
-----------------------

Refactor of the Attribute Storage module
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The Attribute Storage feature is no longer a standalone module and has been
moved to the library core. From the user perspective, this has the following
consequences:

* Explicit installation of this module in runtime is no longer necessary. The
  ``anjay_attr_storage_install()`` method has been removed.
* The ``ANJAY_WITH_MODULE_ATTR_STORAGE`` configuration macro in
  ``anjay_config.h`` has been renamed to ``ANJAY_WITH_ATTR_STORAGE``.
* The ``WITH_MODULE_attr_storage`` CMake option (equivalent to the macro
  mentioned above) has been renamed to ``WITH_ATTR_STORAGE``.

Additionally, the behavior of ``anjay_attr_storage_restore()`` has been
changed - from now on, this function fails if supplied source stream is
invalid and the Attribute Storage remains untouched. This change makes the
function consistent with other ``anjay_*_restore()`` APIs.

Refactor of offline mode control API
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Since Anjay 2.4, offline mode is configurable independently per every
transport. Below is a list of removed functions and counterparts that should
be used:

+--------------------------------+------------------------------------------+
| Removed function               | Counterpart                              |
+--------------------------------+------------------------------------------+
| ``anjay_is_offline()``         | ``anjay_transport_is_offline()``         |
+--------------------------------+------------------------------------------+
| ``anjay_enter_offline()``      | ``anjay_transport_enter_offline()``      |
+--------------------------------+------------------------------------------+
| ``anjay_exit_offline()``       | ``anjay_transport_exit_offline()``       |
+--------------------------------+------------------------------------------+
| ``anjay_schedule_reconnect()`` | ``anjay_transport_schedule_reconnect()`` |
+--------------------------------+------------------------------------------+

New functions should be called with ``transport_set`` argument set to
``ANJAY_TRANSPORT_SET_ALL`` to achieve the same behavior.

Addition of the con attribute to public API
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``con`` attribute, enabled via the ``ANJAY_WITH_CON_ATTR`` compile-time
option, has been previously supported as a custom extension. Since an identical
flag has been standardized as part of LwM2M TS 1.2, it has been included in the
public API as part of preparations to support the new protocol version.

If you initialize ``anjay_dm_oi_attributes_t`` or ``anjay_dm_r_attributes_t``
objects manually, you may need to initialize the new ``con`` field as well,
since the empty ``ANJAY_DM_CON_ATTR_NONE`` value is **NOT** the default
zero-initialized value.

As more new attributes may be added in future versions of Anjay, it is
recommended to initialize such structures with ``ANJAY_DM_OI_ATTRIBUTES_EMPTY``
or ``ANJAY_DM_R_ATTRIBUTES_EMPTY`` constants, and then fill in the attributes
you actually intend to set.

Default (D)TLS version
^^^^^^^^^^^^^^^^^^^^^^

When the `anjay_configuration_t::dtls_version
<../api/structanjay__configuration.html#ab32477e7370a36e02db5b7e7ccbdd89d>`_
field is set to ``AVS_NET_SSL_VERSION_DEFAULT`` (which includes the case of
zero-initialization), Anjay 3.0 and earlier automatically mapped this setting to
``AVS_NET_SSL_VERSION_TLSv1_2`` to ensure that (D)TLS 1.2 is used as mandated by
the LwM2M specification.

This mapping has been removed in Anjay 3.1, which means that the default version
configuration of the underlying (D)TLS library will be used. This has been done
to automatically allow the use of newer protocols and deprecate old versions
when the backend library is updated, without the need to update Anjay code.
However, depending on the (D)TLS backend library used, this may lead to (D)TLS
1.1 or earlier being used if the server does not properly negotiate a higher
version. Please explicitly set ``dtls_version`` to
``AVS_NET_SSL_VERSION_TLSv1_2`` if you want to disallow this.

Please note that Mbed TLS 3.0 has dropped support for TLS 1.1 and earlier, so
this change will not affect behavior with that library.

Conditional compilation for structured security credential support
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``anjay_ret_certificate_chain_info()`` and ``anjay_ret_private_key_info()``
APIs, as well as avs_crypto-based fields in ``anjay_security_instance_t``, have
been put under a new conditional compilation flag,
``ANJAY_WITH_SECURITY_STRUCTURED``.

When using CMake, this flag is enabled by default if available. Otherwise, it
might need to be enabled by defining ``ANJAY_WITH_SECURITY_STRUCTURED`` in
``anjay_config.h``.


Changes in avs_coap
-------------------

Changed flow of cancelling observations in case of errors
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

CoAP observations are implicitly cancelled if a notification bearing a 4.xx or
5.xx error code is delivered.

In Anjay 3.4.x and earlier, this cancellation (which involves calling the
``avs_coap_observe_cancel_handler_t`` callback) was performed *before* calling
the ``avs_coap_delivery_status_handler_t`` callback for the specific
notification. Since Anjay 3.5.0, this order is reversed, so any code that relies
on this logic may break.

This change is only relevant if you are using ``avs_coap`` APIs directly (e.g.
when communicating over raw CoAP protocol) and in case of notifications intended
to be delivered as confirmable. The LwM2M Observe/Notify implementation in Anjay
has been updated accordingly.

Changes in avs_commons
----------------------

Introduction of new socket option
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

avs_commons 4.10.1 bundled with Anjay 2.15.1 adds a new socket option key:
``AVS_NET_SOCKET_HAS_BUFFERED_DATA``. This is used to make sure that when
control is returned to the event loop, the ``poll()`` call will not stall
waiting for new data that in reality has been already buffered and could be
retrieved using the avs_commons APIs.

This is usually meaningful for (D)TLS connections, but for almost all simple
unencrypted socket implementations, this should always return ``false``.

This was previously achieved by always trying to receive more packets with
timeout set to zero. However, it has been determined that such logic could lead
to heavy blocking of the event loop in case communication with the network stack
is relatively slow, e.g. on devices which implement TCP/IP sockets through modem
AT commands.

If you maintain your own socket integration layer or (D)TLS integration layer,
it is recommended that you add support for this option. This is not, however, a
breaking change - if the option is not supported, the library will continue to
use the old behavior.

Refactor of PSK credential handling
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Deprecated ``avs_net_psk_info_t`` structure has been removed. Its successor,
``avs_net_generic_psk_info_t``, has been renamed to ``avs_net_psk_info_t``.
This change also affects ``avs_net_security_info_t`` structure which contains
the latter. Implementation of accompanying ``avs_net_security_info_from_psk()``
function has also been replaced with function previously known as
``avs_net_security_info_from_generic_psk()``.

These changes are breaking for code that accesses the ``data.psk`` field of
``avs_net_security_info_t`` directly and for usages of the two changed types.

Refactor of time handling in avs_sched and avs_coap
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

It is now enforced more strictly that time-based events shall happen when the
clock reaches *at least* the expected value. Previously, the tasks scheduled via
avs_sched were executed only when the clock reached a value *later* than the
scheduled job execution time.

This change will have no impact on your code if your platform has enough clock
resolution so that two subsequent calls to ``avs_time_real_now()`` or
``avs_time_monotonic_now()`` will *always* return different values. As a rule of
thumb, this should be the case if your clock has a resolution no worse than
about 1-2 orders of magnitude smaller than the CPU clock. For example, for a
100 MHz CPU, a clock resolution of around 100-1000 ns (i.e., 1-10 MHz) should be
sufficient, depending on the specific architecture.

If your clock has a lower resolution, you may observe the following changes:

* ``anjay_sched_run()`` is now properly guaranteed to execute at least one job
  if the time reported by ``anjay_sched_time_to_next()`` passed. Previously this
  could require waiting for another change of the numerical value of the clock,
  which could cause undesirable active waiting in the event loop. This is the
  motivating factor in introducing these changes.
* Jobs scheduled using ``AVS_SCHED_NOW()`` during an execution of
  ``anjay_sched_run()`` before the numerical value of the clock changes, *will*
  be executed during the same run. The previous behavior more strictly enforced
  the policy to not execute such jobs in the same run.

If you are scheduling custom jobs through the avs_sched module, you may want or
need to modify their logic accordingly to accommodate for these changes. In most
typical use cases, no changes are expected to be necessary.

Removal of avs_unit_memstream
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``avs_unit_memstream`` was a specific implementation of ``avs_stream_t`` within
the avs_unit module that implemented a simple FIFO stream in a fixed-size memory
area.

This feature has been removed. Instead, you can use an
``avs_stream_inbuf``/``avs_stream_outbuf`` pair, or an ``avs_stream_membuf``
object.
