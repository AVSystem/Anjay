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

Error handling
==============

Since late September 2019 and Anjay 2.2.0 release, ``avs_coap`` has been
refactored to use the ``avs_error_t`` type for error handling.

There are no global or context-wide error states, and specific error codes are
propagated up to public APIs whenever possible.

CoAP-specific errors will have the ``category`` field set to
``AVS_COAP_ERR_CATEGORY``. ``avs_coap`` functions may also return generic
``AVS_ERRNO_CATEGORY`` errors, in particular:

- ``AVS_EBADF`` - if the streaming API is misused, i.e., when the streams are
  used in invalid states
- ``AVS_EBADMSG`` - in case of invalid data encountered during observation
  persist/restore operations
- ``AVS_EINVAL`` - generic "invalid arguments" error
- ``AVS_ENOBUFS`` - buffer overflow in the streaming API
- ``AVS_ENOENT`` - ``avs_coap_client_set_next_response_payload_offset()`` called
  for a nonexistent exchange
- ``AVS_ENOMEM`` - generic "out of memory" error
- ``AVS_ENOMSG`` - OSCORE encryption/decryption error (commercial version only)
- ``AVS_EPROTO`` - message digest calculation error in OSCORE (commercial
  version only)
- ``AVS_ERANGE`` - value out of range when trying to add CoAP option
- other errors, as propagated from the network socket layer

Additional information about errors from the ``AVS_COAP_ERR_CATEGORY`` category
can be obtained using ``avs_coap_error_class()`` and
``avs_coap_error_recovery_action()`` APIs. There is also ``avs_coap_strerror()``
for stringifying such errors, which will automatically delegate to
``avs_strerror()`` for ``AVS_ERRNO_CATEGORY`` errors.
