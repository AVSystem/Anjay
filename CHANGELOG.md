# Changelog

## 3.3.1 (March 10th, 2023)

### Improvements

- `anjay_disable_server()` and `anjay_disable_server_with_timeout()` can now be
  be called on servers that are not enabled as well

### Bugfixes

- Fixed resetting of counter for the Communication Sequence Retry Count resource
- Fixed a regression in 3.2.0 that prevented the bootstrap connection to be
  properly closed if the Bootstrap Server is reconfigured in the new bootstrap
  information and legacy Server-Initiated Bootstrap is disabled

## 3.3.0 (February 21st, 2023)

### Features

- New configuration option, `WITHOUT_MODULE_fw_update_PUSH_MODE` (CMake) / `ANJAY_WITHOUT_MODULE_FW_UPDATE_PUSH_MODE` (header), that allows disabling support for the PUSH mode in the Firmware Update module

### Improvements

- Refactored tests to use `avs_stream_inbuf` instead of `avs_unit_memstream`
- Refactored `anjay_input_ctx_constructor_t` to use only a single pointer for input stream
- Revised support for DTLS Connection ID extension, so that a new handshake is
  not performed if Connection ID is used, unless an error occurs
- Revised example Anjay configurations for embedded builds without CMake to
  optimize compile time and code size

#### Bugfixes

- Fixed a critical regression in 3.2.0 that could cause an assertion failure and use-after-free during Bootstrap Finish if the Bootstrap Server is reconfigured in the new bootstrap information and legacy Server-Initiated Bootstrap is disabled
- Fixed a bug that could cause undefined behavior when reading the Update Delivery Method resource in the Firmware Update object with thread safety enabled but Downloader disabled
- Fixed a bug that prevented notifications from being sent in a timely manner after receiving Reset message cancelling an Observation in response to another confirmable notification
- Fixed a bug that could cause an assertion failure when using `anjay_delete_with_core_persistence()` if a primary server connection failed, but a trigger (SMS) connection is operational
- Fixed the response code of unsuccessful Resource /1/x/9 Bootstrap-Request Trigger execution (e.g. when there is no Bootstrap-Server Account)

## 3.2.1 (December 13th, 2022)

### Improvements

- Added some missing log messages for potential scheduler errors
- Updated the version of pybind11 used by integration tests to 2.10.1

### Bugfixes

- Fixed a regression in 3.2.0 that caused some invalid Writes to be silently ignored without responding with proper error codes
- Fixed compatibility of integration tests with Python 3.11 and the current Github macOS environment

## 3.2.0 (December 7th, 2022)

### BREAKING CHANGES

- Observations are now implicitly canceled when the client's endpoint identity changes (i.e., when the socket is reconnected without a successful DTLS session resumption). This is in line with [RFC 7641](https://datatracker.ietf.org/doc/html/rfc7641#page-22) and LwM2M TS requirements (see [Core 6.4.1](https://www.openmobilealliance.org/release/LightweightM2M/V1_1_1-20190617-A/HTML-Version/OMA-TS-LightweightM2M_Core-V1_1_1-20190617-A.html#6-4-1-0-641-Observe-Operation) and [Transport 6.4.3](https://www.openmobilealliance.org/release/LightweightM2M/V1_1_1-20190617-A/HTML-Version/OMA-TS-LightweightM2M_Transport-V1_1_1-20190617-A.html#6-4-3-0-643-Registration-Interface)), but **may break compatibility with some non-well-behaved servers.**

### Features

- New APIs to access information about the last registration time, next registration update time and last communication with a server time
- Expanded `anjay_resource_observation_status_t` structure so that now `anjay_resource_observation_status()` returns also the number of servers that observe the given Resource (capped at newly introduced `ANJAY_MAX_OBSERVATION_SERVERS_REPORTED_NUMBER`) and their SSIDs

### Improvements

- Migrated GitHub Actions tests from Fedora-36 to RockyLinux-9
- Added compilation flag to enforce Content-Format in Send messages.
- Refactored Firmware Update notification handling and simplified internal module support
- Removed the usage of symbolic links between Python packages to make them usable on Windows
- Key generation in the factory provisioning script has been rewritten to use the cryptography Python module instead of pyOpenSSL
- Factory provisioning script now uses elliptic curve cryptography by default in certificate mode
- `anjay_next_planned_lifecycle_operation()` and `anjay_transport_next_planned_lifecycle_operation()` now properly respect jobs that have been scheduled manually (e.g. `anjay_schedule_registration_update()`)

### Bugfixes

- Fixed a bug that could cause some resources in a Write message to be ignored when they follow a Multiple-Instance Resource entry
- Fixed semantics of Resources 19 and 20 in the Server object, which were mistakenly swapped
  - **NOTE:** The persistence format for the Server object has been reinterpreted so that Resources 19 and 20 remain where they were, without taking semantics into account. This will fix configurations provisioned by Servers but may break configuration persisted just after initially configuring it from code.
- Made sure that `anjay_schedule_registration_update()` forces a single Update request even when followed by `anjay_transport_schedule_reconnect()` or a change of offline mode
- Made sure that notifications are not sent before the Update operation if one has been scheduled
- Made sure that `anjay_transport_schedule_reconnect()` properly reconnects the Bootstrap server connection in all cases
- Made sure that the socket is properly closed when queue mode is enabled, including previously missing cases related to the Send operation and when no CoAP message needs to be sent at all
- Refactored asynchronous server connection management to avoid race conditions that could lead to required actions (e.g. EST requests) not being performed when the calculated delays were not big enough

## 3.1.2 (August 24th, 2022)

### Improvements

- Reduced code size of the Security object implementation
- Updated documentation, readme and examples to mention the new EU IoT Cloud platform
- Migrated GitHub Actions tests to ubuntu-18.04, ubuntu-20.04, ubuntu-22.04, fedora-36 and macos-11

### Bugfixes

- Fixed various compilation warnings
- Fixed dangerous usage of `avs_realloc()` in the event loop implementation

## 3.1.1 (July 22nd, 2022)

### Improvements

- Added `CHANGELOG.md`

### Bugfixes

- Added the missing return in anjay_dm_handlers.c that could cause undefined behavior when `ANJAY_WITH_THREAD_SAFETY` was disabled
- Removed the unused option in the factory provisioning script
- Removed usage of Python 3.6 syntax in tests that caused Github Actions tests to fail
- Added missing notes about the change to (D)TLS version in all migration guides in the documentation
- (commercial feature only) Fixed proper handling of changing the disable_legacy_server_initiated_bootstrap across core persistence cycles

## 3.1.0 (July 6th, 2022)

### BREAKING CHANGES

**Note:** the following changes, while technically breaking, are minor, and should not cause problems in most pratical usages. See also: https://avsystem.github.io/Anjay-doc/Migrating/MigratingFromAnjay30.html

- Changed error handling semantics of anjay_attr_storage_restore() to match other persistence restore functions
- TLS 1.2 is no longer implicitly set as the default (D)TLS version; the underlying crypto library's default is now used

### Features

- Factory provisioning feature that allows to perform "Factory bootstrap" based on SenML CBOR data stream
- New API: anjay_access_control_set_owner(), allowing to set Owner resource in the Access Control object during the "Factory bootstrap" phase
- New APIs for changing the CoAP transmission parameters, CoAP exchange timeout and DTLS handshake timeouts while the library is running

### Improvements

- Migrated the Observe/Notify subsystem to use the new AVS_SORTED_SET API from avs_commons; this means that avs_rbtree can be disabled, in which case a more lightweight list-based implementation will be used
- Minor code size optimizations in the Server object implementation
- Added documentation for the OSCORE commercial feature
- (D)TLS version can now be set from command line in the demo application

### Bugfixes

- Fixed a bug in anjay_ongoing_registration_exists() that could cause it to always return true if disable_legacy_server_initiated_bootstrap is set to true
- Fixed improper formatting of the payload describing the data model in the Register message during initial negotiation of the LwM2M version
- Fixed handling of persistence format versioning for the Security object, that could cause crashes if Anjay was compiled without LwM2M 1.1 support
- Changed the "Bootstrap on Registration Failure" resource in the Server object to be readable, as specified in LwM2M TS 1.2
- (commercial feature only) Added persistence of runtime LwM2M version in the core persistence feature; previously the client could erroneously use a different LwM2M version than it registered with after core persistence restore

## 3.0.0 (May 18th, 2022)

### BREAKING CHANGES

- Changed license of the free version to AVSystem-5-clause
- Refactored the attr_storage module as a core feature
  - Names of the relevant CMake options and configuration macros have changed
  - anjay_attr_storage_install() has been removed; Attribute Storage is  now always installed if enabled at compilation time
  - Behavior of anjay_attr_storage_restore() has been changed - this function now fails if supplied source stream is empty
- The "con" attribute is now included in anjay_dm_oi_attributes_t, as it has been standardized for LwM2M TS 1.2
- Refactored public headers to consistently use conditional compilation; APIs for disabled features are no longer accessible
- Removed previously deprecated APIs
- avs_commons 5.0 refactor the API for providing PSK credentials. Please refer to the change log there, or the document below for details: https://avsystem.github.io/Anjay-doc/Migrating/MigratingFromAnjay215.html

### Features

- LwM2M TS 1.1 support and related features are now available in the open source version; the features include:
  - Support for TCP binding
  - Support for SenML JSON, SenML CBOR and raw CBOR content formats
  - Support for the Send operation
  - Possibility for automatically moving security credentials provisioned by the Bootstrap Server or the bootstrapper module onto hardware security engines (note: no hardware security engine implementation is provided in the open source version)
- Security credentials provisioned by the Bootstrap server or bootstrapper module and automatically moved onto hardware security engine can now be marked as "permanent" to prevent them from being removed
- (commercial feature only) Experimental support for some LwM2M TS 1.2 features

### Improvements

- Refactored incoming message handling to make use of the `AVS_NET_SOCKET_HAS_BUFFERED_DATA` feature added in avs_commons 5.0
- Refactored and simplified internal flow of calling data model handlers
- Refactored internal handling of communication state
- Commercial features are now available for separate inclusion, described in the documentation more clearly and feature code examples
- Various improvements in the documentation

## Anjay 2.15.0 (April 8th, 2022)

### Bugfixes

- Fixed some uninitialized variables in IPSO object implementations
- Fixed some compilation warnings in unit tests
- Fixed compatibility of integration tests with OpenSSL 3
- Fixed socket flag handling in tests that were breaking with some versions of Python
- Fixed some obsolete information in Doxygen documentation
- (commercial feature only) Added a validity check for the certificate provisioned via EST; previous code could lead to an assertion failure if the server misbehaved or the system clock was not set correctly

### BREAKING CHANGES

- avs_commons 4.10 contains a refator of PSK security credential handling. Please refer to the change log there, or the document below for details: https://avsystem.github.io/Anjay-doc/Migrating/MigratingFromAnjay214.html

### Features

- Added a new anjay_event_loop_run_with_error_handling() API that automatically restarts the communication in case of a fatal error
- (commercial version only) Added support for using PSK security credentials located in hardware security engines
- (commercial version only) Added the possibility for automatically moving security credentials provisioned by the Bootstrap Server or the bootstrapper module onto hardware security engines

### Improvements

- Added proper support for object versioning in the object stub generator
- Refactored LOG_VALIDATION_FAILED macros in security and server modules to prevent some compilers to generate code with excessive stack usage
- Stopped using LOG macro in expression context for better compatibility with external logger implementations
- Added support for Mbed TLS 3.1 in the pymbedtls module used in tests

### Bugfixes

- Fixed firmware update protocol support being erroneously reported when a custom TLS layer is used
- Changed default "Minimum Period" attribute value to 0, as mandated by the specification
- Fixed a potential memory leak in fw_update module's cleanup routine
- Fixed the downloader and fw_update modules erroneously passing empty CoAP ETags to the user
- Fixed ETag handling in the firmware update tutorial examples
- Fixed handling of objects with names that contain characters that are invalid for C identifiers in the object stub generator
- Fixed building documentation on newer versions of Sphinx
- Prevented the custom TLS layer from building during testing if the dependencies are not met
- (commercial version only) Fixed the wrong resource being written when updating the TLS/DTLS Alert Code resource
- (commercial version only) Fixed a bug which could cause communication over coaps+tcp to be stalled due to buffering on the TLS layer

Also updates avs_commons to version 4.10.0. For details, see https://github.com/AVSystem/avs_commons/releases/tag/4.10.0

## Anjay 2.14.1 (November 29th, 2021)

### Features

- added custom TLS layer tutorial,
- added possibility to generate code for objects with constant number of instances.

### Improvements

- updated avs_commons to version 4.9.1,
- made avs_coap work with external logger feature,
- expanded code generation docs.

### Bugfixes

- fixed erronous links in documentation,
- removed issues from IPSO objects documentation.

## Anjay 2.14.0 (October 1st, 2021)

### Features

- Added anjay_event_loop_run() and anjay_serve_any() APIs that remove the need to implement the event loop manually in client applications
- Added predefined implementation of some common IPSO object types
- (commercial version only) Support for PSA API for hardware-based security

### Improvements

- Moved mutex locking from anjay_sched_run() to scheduler jobs themselves, so that custom scheduler jobs are properly supported when thread safety is enabled; this is technically a breaking change against 2.13.0, but the 2.13.0 behaviour has been classified as a defect
- Refactored the demo client to use the new event loop API
- Made data types used by anjay_codegen.py more consistent
- Various improvements and updates to the documentation, including new tutorials about thread safety and the new APIs
- Improved integration tests so that they are more deterministic
- Migrated public CI for the open-source version to GitHub Actions

### Bugfixes

- Fixed incorrect behavior of anjay_ongoing_registration_exists() when Server-Initiated Bootstrap was in use (issue #56)
- Fixed some potential race conditions that could cause anjay_get_socket_entries() to return invalid sockets when downloads were in progress
- Fixed obsolete URLs in documentation

Also updates avs_commons to version 4.9.0. For details, see: https://github.com/AVSystem/avs_commons/releases/tag/4.9.0

## Anjay 2.13.0 (July 19th, 2021)

### Features

- Added optional support for thread safety When enabled, all calls to Anjay library functions are protected using built-in mutex, which allows safe integration into multi-threaded applications.

### Improvements

- General improvements when using fw_update module without ANJAY_WITH_DOWNLOADER
- Add option in demo to provide identity and psk as ASCII string

### Bugfixes
- Fixed URL in lwm2m_object_registry.py
- Fix bug in dockerfile with apt-get install change not refreshing apt index

Also updates avs_commons to version 4.8.1. For details, see: https://github.com/AVSystem/avs_commons/releases/tag/4.8.1

## Anjay 2.12.0 (June 30th, 2021)

### Features

- Added extended log handler implementation, it can be enabled in demo by `--alternative-logger` argument.

### Improvements

- Endpoint name and local MSISDN are copied during anjay_new() along with the rest of the parameters.
- Demo now checks if binding mode is compatible with the provided URI.

### Bugfixes

- Fixed the case where the LwM2M server requests a block from the middle.
- Handle fatal CoAP errors during registration, in case of aborted context device will abort registration. (commercial version only)

Also updates avs_commons to version 4.8.0. For details, see: https://github.com/AVSystem/avs_commons/releases/tag/4.8.0

## Anjay 2.11.1 (June 2nd, 2021)

### Features

- Added anjay_send_batch_data_add_current_multiple_ignore_not_found(), a variant of anjay_send_batch_data_add_current_multiple() that does not treat non-existing resources as an error

### Improvements

- Simplifications in JSON serialization code
- Relaxed timeout values in some integration tests
- Added documentation for internal CoAP packet parsing APIs
- (commercial version only) Read-Composite and Observe-Composite responses now make use of the "base name" and "base time" SenML labels
- (commercial version only) Observe response sequence number is now omitted for the TCP transport, as permitted by RFC 8323

### Bugfixes

- Fixed default content-format selection for simple resources if it is not selected by the server and text/plain is disabled at compile time
- (commercial version only) Fixed a critical bug in anjay_send_batch_data_add_current_multiple() that could cause the batch to be in an inconsistent state if the underlying read operation failed
- (commercial version only) Fixed a potential memory leak when restoring observation state in anjay_new_from_core_persistence()

Also updates avs_commons to version 4.7.2. For details, see: https://github.com/AVSystem/avs_commons/releases/tag/4.7.2

## Anjay 2.11.0 (April 29th, 2021)

### Features

- (commercial version only) Added anjay_send_batch_data_add_current_multiple() API for sending multiple resources with the same timestamp
- (commercial version only) "send" command in demo application now supports sending multiple resources at once

### Improvements

- Added documentation for the avs_coap module
- Added tutorial for using the LwM2M Send method (available in commercial version only)
- (commercial version only) Resource Instances can now be created through the Write-Composite operation
- (commercial version only) Made use of Base Name and Base Time labels when generating SenML documents to reduce message size

### Bugfixes

- Enforced decimal base when handling text/plain content format (previously C-style octal and hexadecimal literals were erroneously supported)
- Made anjay_ongoing_registration_exists() work even if non-default implementation of the Server object is in use
- Fixed various compilation warnings
- (commercial version only) Removed erroneous quotes when reporting LwM2M Enabler version in Register requests and Discover responses for LwM2M 1.1
- (commercial version only) Proper checks for attempts to send data from forbidden objects (Security, OSCORE) via anjay_send_batch_data_add_current()
- (commercial version only) Added graceful handling of the case when security information provisioned through EST onto an HSM is not accessible, but necessary to attempt bootstrap

Also updates avs_commons to version 4.7.1. For details, see: https://github.com/AVSystem/avs_commons/releases/tag/4.7.1

## Anjay 2.10.0 (March 19th, 2021)

### Features

- Added additional_tls_config_clb field to anjay_configuration_t that allows for advanced configuration of the TLS backend
- Added possibility to mix NoSec and secured server connections in demo client
- Added pretty-printing of JSON payloads in the NSH testing shell
- (commercial version only) Added APIs to query times of upcoming Register, Update and Notify events
- (commercial version only) Implemented reporting of Short Server ID for the OSCORE object on Bootstrap-Discover, as required by LwM2M 1.1
- (commercial version only) Added support for the Trigger resource in the Server object
- (commercial version only) Added support for the ID Context resource in the OSCORE object

### Improvements

- Refactored _anjay_sync_access_control() to avoid recursion
- Refactored debug logging of CoAP BLOCK options

### Bugfixes

- Fixed the issue where pmax-based notifications were not being scheduled when Notification Storing was disabled
- Fixed semantics of the LwM2M Write operation when writing optional resources that are not supported in the implementation; this behaviour has been clarified between LwM2M TS releases 1.1 and 1.1.1
- Fixed queue-mode server connections superfluously resuming when exiting offline mode if there is no data to be sent
- Fixed potential NULL dereferences in Security, Server, Access Control and OSCORE object implementations
- Fixed potential memory leak in data batch storage module (used by Notify and Send operations)
- More robust state checking in the TLV output module
- Fixed some linting checks (visibility, header and code duplication verification) that were not executing properly as part of "make check" and deduplicated them between subprojects
- Fixed a bug in pymbedtls module that caused integration tests to sometimes freeze indefinitely
- Fixed a bug that prevented lwm2m_decode command in the NSH testing shell from working
- Fixed a problem with building the demo application on Windows
- (commercial version only) Made LwM2M Send operation work properly when the server connection is suspended due to queue mode operation
- (commercial version only) Fixed handling of buffer overflow corner cases in the BG96 driver in the demo application

Also updates avs_commons to version 4.7. For details, see: https://github.com/AVSystem/avs_commons/releases/tag/4.7

## Anjay 2.9.0 (January 18th, 2021)

### BREAKING CHANGES

- Minimum required CMake version is raised to 3.6
- avs_commons 4.6 contains a refactor of avs_net_local_address_for_target_host() that may be breaking for users who maintain their own socket integration code

For more detailed information about breaking changes and how your code needs to be updated, see: https://avsystem.github.io/Anjay-doc/Migrating/MigratingFromAnjay28.html

### Features

- LwM2M Testing Shell is now included in the open-source version, see https://avsystem.github.io/Anjay-doc/Tools/CliLwM2MServer.html
- Demo application can now be built even when some of the optional library features (e.g. bootstrap, Access Control, observation support, persistence) are disabled
- A new guide for writing custom socket integration code, and example lightweight implementation: https://avsystem.github.io/Anjay-doc/PortingGuideForNonPOSIXPlatforms/NetworkingAPI.html

### Improvements

- Improved wording in migration documentation to make usage clearer
- Various improvements to integration tests:
  - Tests that involve restarting the demo process now retain execution logs for each launch
  - Fixed a race condition when handling demo process shutdown
  - (commercial version only) Additional tests for rebuilding client certificate chain
- (commercial version only) CoAP message cache, previously only enabled for UDP, is now also used for SMS and NIDD transports
- (commercial version only) Made closing NIDD connection in the BG96 driver more resilient to errors

### Bugfixes

- Removed some misleading log messages
- (commercial version only) Fixed some dependencies between CMake configuration options so that cmake -DDTLS_BACKEND= . works with default settings

Also updates avs_commons to version 4.6 which, in addition to the breaking change mentioned above, introduces the following changes:

### Improvements

- Additional tests for the avs_stream module

Bugfixes
- Fixed erroneous bounds check in _avs_crypto_get_data_source_definition()
- Made removal of PKCS#11 objects more resilient to errors (relevant mostly for commercial Anjay users)
- Fixed CMake code for importing the libp11 library (relevant mostly for commercial Anjay users)

## Anjay 2.8.0 (November 23rd, 2020)

### BREAKING CHANGES

See below for breaking changes in avs_commons. Note that these are unlikely to affect users that use CMake for building the library, but may require updating configuration headers when using alternative build systems.

For more detailed information about breaking changes and how your code needs to be updated, see: https://avsystem.github.io/Anjay-doc/Migrating/MigratingFromAnjay27.html

### Features

- Made use of avs_commons' new floating point formatting functions, making it possible to link against libc implementations that don't support printf("%g")
- (commercial version only) Support for Enrollment over Secure Transports (EST-coaps) with key and certificate storage on Hardware Security Modules via avs_commons' OpenSSL engine support
- (commercial version only) Support for certificate chain reconstruction based on trust store when performing (D)TLS handshake - this is especially useful for EST-based security, as certificates provisioned via /est/crts can be used as client certificate chain during handshake

### Improvements

- Better CMake-level dependencies and error handling for compile-time configuration options
- Fixed various compile-time warnings
- Included avs_commons and avs_coap configuration in the TRACE-level configuration report log at initialization time
- Relaxed timeout for Deregister message in integration tests
- Integration test target logs path is now configurable in runtest.py
- Various improvements to documentation and examples:
  - Attribute storage module is now installed in most tutorials, making them more complete
  - https://avsystem.github.io/Anjay-doc/AdvancedTopics/AT-NetworkErrorHandling.html now mentions retry behavior of the commercial version in LwM2M 1.1
  - API documentation generated by Doxygen now properly includes all commercial-only APIs when run in commercial codebase
  - Updated installation instructions for CentOS that referred to non-existent URLs
  - Updated visual style to match corporate identity

### Bugfixes

- Data model persistence routines can no longer be successfully called during the bootstrap procedure, preventing from persisting potentially invalid data
- Fixed a regression in 2.7.0 that prevented ciphersuite setting from being properly respected for HTTP downloads
- Fixed a bug that could result in an assertion failure when showing demo client's help message
- Fixed a bug in "get_transport" command implementation in demo client
- Fixed erroneous setting of AVS_COMMONS_WITH_AVS_CRYPTO_ADVANCED_FEATURES in example configuration headers
- Removed duplicate file names that could prevent building with some embedded IDEs

Also updates avs_commons to version 4.5 which introduces the following changes:

### BREAKING CHANGES

- Moved URL handling routines to a separate avs_url component
- Implementation of avs_net_validate_ip_address() is no longer required when writing custom socket integration layer
- Hardware Security Module support has been reorganized to allow easier implementation of third-party engines

### Features

- Support for private key generation and removal on Hardware Security Modules via PKCS#11 engine
- Support for storing and removing certificates stored on Hardware Security Modules via PKCS#11 engine
- Support for certificate chain reconstruction based on trust store when performing (D)TLS handshake
- New AVS_DOUBLE_AS_STRING() API and AVS_COMMONS_WITHOUT_FLOAT_FORMAT_SPECIFIERS configuration options, making it possible to stringify floating point numbers on libc implementations that don't support printf("%g")

### Improvements

- Simplified URL hostname validation - it is now somewhat more lenient, but no longer depends on avs_net_validate_ip_address()
- Removed internal usage of avs_net_validate_ip_address() and reimplemented it as an inline function that wraps avs_net_addrinfo_resolve_ex()
- Better CMake-level dependencies and compile-time error handling for compile-time configuration options
- PEM-formatted security objects can now be loaded from buffer in the Mbed TLS backend

### Bugfixes

- Fixed conditional compilation clauses for avs_crypto global initialization
- Additional NULL checks when loading security information
- Removed duplicate file names that could prevent building with some embedded IDEs

## Anjay 2.7.0 (October 15th, 2020)

### BREAKING CHANGES

- Changed signature of anjay_security_config_from_dm() and expected lifetime of anjay_security_config_t; removed anjay_fw_update_load_security_from_dm() compatibility alias

Note: For a more detailed information about breaking changes and how your code needs to be updated, see: https://avsystem.github.io/Anjay-doc/Migrating/MigratingFromAnjay26.html

### Features

- New anjay_download_set_next_block_offset() API that allows skipping parts of downloads
- (commercial version only) Support for configuring certificate security from external sources during the Factory Bootstrap phase, including initial support for PKCS11-based hardware security
- (commercial version only) Support for TCP in the NSH testing shell

### Bugfixes

- Fixed support for older versions of Mbed TLS
- More graceful error handling in anjay_security_object_add_instance()
- Made the option to disable bootstrap support work again
- Other bug fixes, partially found using fuzz testing

Also updates avs_commons to version 4.4 which introduces the following changes:

### BREAKING CHANGES

- Significant refactor of avs_crypto_security_info_union_t family of types (compatibility aliases are available)

### Features

- Initial support for PKCS11-based hardware security
- New APIs:
  - avs_crypto_certificate_chain_info_array_persistence()
  - avs_crypto_certificate_chain_info_from_engine()
  - avs_crypto_certificate_chain_info_list_persistence()
  - avs_crypto_certificate_chain_info_persist()
  - avs_crypto_cert_revocation_list_info_array_persistence()
  - avs_crypto_cert_revocation_list_info_list_persistence()
  - avs_crypto_cert_revocation_list_info_persist()
  - avs_crypto_private_key_info_copy()
  - avs_crypto_private_key_info_from_engine()
  - avs_crypto_private_key_info_persistence()
  - avs_net_socket_dane_tlsa_array_copy()
  - avs_stream_copy()
  - avs_stream_offset()
- Added scripts simplifying unit test code coverage calculation

## Anjay 2.6.1 (August 31st, 2020)

### Features

- Added documentation for the LwM2M testing shell (NSH) - note that the shell itself is only available in the commercial version

### Improvements

- Refactored security key loading flow

### Bugfixes

- Fixed testing scripts to make them work on macOS and Raspberry Pi OS again
- Added __odr_asan to the list of permitted symbols so that "make check" succeeds when the library is built with AddressSanitizer enabled
- (applicable to commercial version only) Fixed a bug in anjay_server_object_set_lifetime() that could lead to sending the Update message twice afterwards

Also updates avs_commons to version 4.3.1 which introduces the following changes:

### Improvements

- Replaced the test PKCS#7 file in unit tests with a more modern one, that can be loaded properly with newest releases of Mbed TLS

### Bugfixes

- Made the library compile again with Mbed TLS configured without CRL support or without file system support
- Fixed some testing code to make it work on macOS and Raspberry Pi OS again
- Added __odr_asan to the list of permitted symbols so that "make check" succeeds when the library is built with AddressSanitizer enabled

## Anjay 2.6.0 (August 25th, 2020)

### Features

- Added compile-time option to disable plaintext and TLV format support
- Added compile-time option to disable usage of the Deregister message
- Added support for DANE TLSA entries for downloads
- Added support for Security and Server (and OSCORE in commercial version) persistence to the demo client
- (commercial version only) More complete support for Enrollment over Secure Transports (EST-coaps), including:
  - Support for /est/sren and /est/crts operations
  - Persistence of EST data
  - Support for application/pkcs7-mime;smime-type=certs-only content format
- (commercial version only) Implemented OSCORE object persistence
- (commercial version only) Support for Matching Type and Certificate Usage Resources in the LwM2M Security object
- (commercial version only) LwM2M Security object Resources that has previously been only supported through Bootstrap Write, are now also exposed through anjay_security_instance_t

### Improvements

- Anjay can now be used on platforms that do not support handling 64-bit integers through printf() and scanf()
- Stricter command line option parsing in the demo client
- Improved help message formatting in the demo client
- (commercial version only) Various improvements to TLV and CBOR handling in the nsh tool

### Bugfixes

- Fixed a bug that could lead to a blocking receive with infinite timeout when DTLS is in use and handshake messages had to be retransmitted
  - NOTE: This, strictly speaking, introduces a BREAKING CHANGE in semantics of `anjay_serve()` and `avs_coap_*_handle_incoming_packet()` - as they no longer wait for the first message to arrive, but handle it in a non-blocking manner. However, this should not matter in practice if recommended patterns of these functions' usage are followed, and in the worst case scenario it may cause poorly written event loop code to behave as a busy loop, but it should not prevent the code from working.
- Stricter parsing of TLV payloads
- Fixed calculation of block size when resuming CoAP downloads which did not use the ETag option
- Made tests related to X.509 certificate mode pass with the OpenSSL backend
- More graceful error handling in downloader when required callbacks are not passed
- Fixed various compilation warnings
- (commercial version only) Fixed a bug that could lead to crash if offline mode was toggled during same-socket CoAP download and another LwM2M exchange was also scheduled
- (commercial version only) Fixed CMake option dependencies for the EST feature

Also updates avs_commons to version 4.3 which introduces the following changes:

### Features

- Improved trust store handling, including:
  - Support for configuring usage of system-wide trust store
  - Support for trusted certificate arrays and lists in addition to single entries
  - Support for CRLs
- Support for DANE TLSA entries
- Support for loading certs-only PKCS#7 files
- New avs_crypto_client_cert_expiration_date() API
- Removed dtls_echo_server tool that has been unused since version 4.1

### Bugfixes

- Fixed a bug that prevented compiling avs_commons without TLS support
- Fixed missing error handling in avs_persistence_sized_buffer()
- Fixed a bug in safe_add_int64_t() that could cause a crash if the result of addition was INT64_MIN
- Fixed various compilation warnings

## Anjay 2.5.0 (July 14th, 2020)

### Features

- Updated AvsCommons to 4.2.1
- Added new API for etag allocation
- (commercial version only) Added initial support for Enrollment Over Secure Transport (EST)

### Bugfixes

- Fixed segfault in CoAP downloads caused by cancellation in the middle of the transfer
- Fixed building tests on CentOS
- Fixed compilation when WITH_ANJAY_LOGS=OFF is used
- Fixed handling of transactional LwM2M Write
- (commercial version only) Fixed download suspension for downloads over shared socket

## Anjay 2.4.4 (July 1st, 2020)

### Features

- Updated avs_commons to version 4.2.0
- (commercial version) Added API for run-time Lifetime management

### Bugfixes

- (commercial version) Fixed corner case handling of Last Bootstrapped Resource

## Anjay 2.4.3 (June 25th, 2020)

### Improvements

- Added workarounds for non-deterministic operation of time-sensitive integration tests

### Bugfixes

- Fixed a critical bug in error handling of notification sending
- Fixed some bugs in Docker and Travis integration
- (commercial version only) Fixed a bug in `anjay_transport_*()` functions that prevented them from working correctly with NIDD transport

## Anjay 2.4.2 (June 17th, 2020)

### Features

- (commercial version only) Implemented NIDD MTU management, allowing to configure maximum message size to be sent, and the maximum message to be received
- (commercial version only) Added binding mode deduction from URI scheme to demo client

### Improvements

- Added Dockerfile to simplify compiling & launching Anjay demo client on various systems
- Improved error reporting when executing Registration Update Trigger

### Bugfixes

- Fixed incorrect condition in time_object_notify in tutorials' code
- Fixed compilation issues of pymbedtls on newer GCC versions
- Fixed compilation of the demo client on Windows

## Anjay 2.4.1 (May 29th, 2020)

- NOTE: The endpoint name and server URI arguments to the demo client are now mandatory

### Improvements

- Fixed various compilation warnings in certain configurations
- Updated documentation, readme and examples to mention the new Try Anjay platform
- Added some missing information in "Porting guide for non-POSIX platforms" documentation article
- Additional script for testing Docker configurations used by Travis locally

### Bugfixes

- Updated avs_commons to version 4.1.3, with more fixes in CMake scripts for corner cases when searching for mbed TLSa, and fix for allowing compilation on platforms that define macros that conflict with avs_log verbosity levels (DEBUG, ERROR etc.)
- Removed the .clang-format file that relied on features specific to an unpublished custom fork of clang-format

## Anjay 2.4a (May 22nd, 2020)

Updated avs_commons to version 4.1.2, which fixes interoperability problem with CMake versions older than 3.11.

## Anjay 2.4 (May 21st, 2020)

### Features

- Added anjay_ongoing_registration_exists() API
- Added anjay_server_get_ssids() API for the default implementation of the Server object
- Made offline mode configurable independently per transport (UDP, TCP; in commercial version also SMS and NIDD) and respected by downloads (including firmware update)
- Network integration layer (in commercial version, also SMS and NIDD drivers) may now use avs_errno(AVS_ENODEV) as a special error condition that will NOT trigger connection reset
- (commercial version only) Added avs_send_deferrable() API
- (commercial version only) Support for reporting State and Result changes using LwM2M Send messages in the fw_update module
- (commercial version only) Ability to perform CoAP(S) downloads (including firmware update) over the same socket that is already used for LwM2M communication. In particular, this allows downloads over SMS and NIDD.

### Improvements

- fw_update module now allows reset of the state machine during download
- Ability to report successful firmware update without reboot through the fw_update module is now officially supported
- (commercial verison only) OSCORE implementation now properly supports kid_ctx negotiation as specified by RFC 8613 Appendix B.2
- (commercial version only) Improvements to NIDD handling, to make sure that different packet size limits may be used for incoming and outgoing messages (NOTE: For the commercial version, this includes BREAKING CHANGES)

### Bugfixes

- Made some payload processing errors (including text/plain base64 decoding errors) return 4.00 Bad Request properly instead of 5.00 Internal Server Error
- The default Disable timeout in the default implementation of the Server object is now 86400 as mandated by the spec instead of infinity
- (commercial version only) Fixed a bug in timeout handling that sometimes caused the bg96_nidd driver to report spurious errors

Also updates avs_commons to version 4.1.1, which includes the following changes:

### Bugfixes

- Fixed a bug in CMake scripts that caused link errors when using statically linked versions of mbed TLS
 ## Anjay 2.3a (May 15th, 2020)
 BREAKING CHANGES:
- Removed usages of the ssize_t type. APIs in both Anjay and avs_commons that had it in public signatures have been redesigned
- (commercial version only) Retry mechanisms described in LwM2M TS 1.1.1, section 6.2.1.2 are now used by default, which changes the default registration retry policy
- Updated avs_commons to version 4.1, with the following breaking changes:
  - Renamed public header files for better uniqueness
  - Redesigned socket creation and in-place decoration APIs, including the addition of a requirement to provide PRNG context
  - Renamed some public configuration macros, to unify with the updated compile-time configuration pattern
  - Removed the legacy avs_coap component (the version used by Anjay 1.x)
  - Removed the mbed TLS custom entropy initializer pattern in favor of the new PRNG framework

Note: For a more detailed information about breaking changes and how your code needs to be fixed, see https://avsystem.github.io/Anjay-doc/Migrating/MigratingFromAnjay225.html

### Features

- Changed project structure, configuration headers and updated build system, so that building the library without using CMake is now officially supported
- Allowed public access to Anjay's scheduler using anjay_get_scheduler()
- Code generator now allows omitting some of the resources during generation
- (commercial version only) Added support for some of the retry mechanisms described in LwM2M TS 1.1.1, including the following resources:
  - Bootstrap on Registration Failure
  - Communication Retry Count
  - Communication Retry Timer
  - Communication Sequence Delay Timer
  - Communication Sequence Retry Count
- (commercial version only) SMS driver API is now public, allowing for custom driver implementation

### Improvements

- Major overhaul of documentation, including new tutorials for data model and firmware update implementation, as well as guides for migration from both Anjay 1.16 and 2.2
- Cryptographically secure PRNG is now used whenever possible for generation of CoAP tokens and initial message IDs
- Improvements to code generator:
  - Refactored the generated code so that programmatic instantiation of Object Instances is now easier
  - Code generated in C++ mode is now more object-oriented and idiomatic
- Thanks to all compile-time configuration now being accessible via a public header, the demo client can now be compiled even when some optional features are disabled
- Removed some unused code from the open source version

### Bugfixes

- Fixed a problem that could occur while reconnecting to a server, if the host's local address was switching between IPv4 and IPv6
- Fixed behavior of anjay_exit_offline() when called immediately after anjay_enter_offline()
- disable_legacy_server_initiated_bootstrap is now properly respected when reconnecting after a connectivity failure
- anjay_get_string() and anjay_execute_get_arg_value() now report buffer underruns more reliably
- Added missing HTTP download timeout logic
- Errors from the socket's send() method are now properly propagated through the CoAP layer
- Fixed fatal errors that could occur during some specific CoAP error conditions, including during attempts to silently ignore incoming packets
- Fixes to minor issues found by Coverity
- Fixed working on platforms where malloc/calloc returns NULL when 0 bytes is requested
- Fixed compatibility with some embedded compilers
- Fixed interoperability with servers that use Uri-Path: '' to represent empty query path, as it seems to be permitted by the CoAP RFC
- Fixed error codes used by the Portfolio object in the demo client
- Minor fixes in integration testing framework
- (commercial version only) Added missing support of LwM2M 1.1-specific resources of the Server object to its persistence functions
- (commercial version only) Fixed various bugs in the bootstrapper module
- (commercial version only) Additional verification of NIDD URLs
- (commercial version only) More robust error handling in demo client's NIDD driver

Also updates avs_commons to version 4.1, which includes the following changes, in addition to those mentioned above as "breaking changes"


### Features

- Building without CMake is now officially supported
- Added idiomatic C++ wrapper for AVS_LIST
- New API for cryptographically safe PRNGs in avs_crypto
- File-based streams and default log handler can now be disabled at compile time

### Bugfixes

- Fixed a bug in the default socket implementation that prevented compiling on platforms without the IP_TOS socket option support
- Fixed improper parsing of empty host in URLs
- Some previously missed log messages now properly respect WITH_AVS_MICRO_LOGS
- Fixed a bug in netbuf stream's error handling

## Anjay 2.2.5 (February 7th, 2020)

### Bugfixes

- Updated avs_commons to version 4.0.3, which includes:
  - Fix for scope of avs_net_mbedtls_entropy_init() declaration in deps.h
  - Fix that prevented net_impl.c from compiling when IP_TOS is not available

## Anjay 2.2.4 (January 30th, 2020)

### Bugfixes

- Fixed bugs that caused problems with compilation on macOS and Travis
- avs_commons 4.0.2 include a fix to TLS backend data loader unit tests

### Features

- Added support for "micro logs", removing most of log strings to save space, while retaining all the information useful for debugging
- avs_commons 4.0.2 include support for proper RFC 6125-compliant validation of certificates against hostnames in the OpenSSL backend


## Anjay 2.2.3 (January 17th, 2020)

### Bugfixes

- Fixed error in CoAP message ID assignment when a CoAP request was being sent from a response handler

### Features

- Added anjay_resource_observation_status() API to the open source version
- Added --server-public-key-file to the demo application

## Anjay 2.2.2 (December 20th, 2019)

### Bugfixes

- Fixed an assertion failure on Cancel Observe arriving while sending a confirmable notification

### Improvements

- Minor workarounds for various compiler warnings
- Fixed unnecessary building of CoAP library test targets

Also updates avs_commons to version 4.0.1, which includes the following changes:

### Bugfixes

- Prevented certificate-based ciphersuites from being sent in Client Hello when PSK is used over the OpenSSL backend

### Features

- Introduced "micro log" feature and AVS_DISPOSABLE_LOG() macro

## Anjay 2.2.1 (December 6th, 2019)

This release synchronizes the open-source version of Anjay with the commercial branch, that has been in development since September 2018. Versions 2.0.0 (June 14th, 2019) through 2.2.0 have only been released to commercial customers.

Note that the commercial version includes extensive support for LwM2M TS 1.1 features. These are not available in the open-source version and not described in this changelog.

### BREAKING CHANGES

- Redesigned data model APIs
  - Replaced instance_it and instance_present handlers with list_instances
  - Simplified the instance_create API
    - IIDs for Create are always assigned by Anjay - user code no longer needs to allocate IDs
    - Removed the SSID argument
  - Replaced supported_rids field, resource_present, resource_it and resource_operations handlers with list_resources
  - Redesigned handling of Multiple-Instance Resources:
    - Old APIs for resource arrays are no longer available
    - Read and write handlers now take additional Resource Instance ID argument
    - Removed resource_dim handler
    - Introduced new resource_reset and list_resource_instances handlers
- Renamed various types, in particular those related to LwM2M Attributes
- Disallowed 65535 for all levels of IDs, as mandated in LwM2M TS 1.1
- Changed custom objects in demo client to use Object IDs from the range of Bulk Objects Reserved by AVSystem
  - Additional minor changes to the Test object semantics
- It is now not possible to build both static and shared versions of Anjay as part of the same build
- Heavily refactored error handling; some public APIs may require use of avs_error_t instead of plain integer error codes
- Removed stubs of commercial-only APIs from the open source version

### Features

- Entirely rewritten CoAP implementation
- Register, Update, Request Bootstrap and confirmable Notify messages are now sent asynchronously and do not block other functionality
- Notifications in JSON format now include timestamps
- Added setting to prefer hierarchical Content-Format even when reading simple resources, to improve interoperability with certain server implementations
- Added support for DTLS Connection ID extension if using a development version of mbed TLS that supports it
- Added ability to configure (D)TLS ciphersuites
- Added support for epmin and epmax attributes, specified in LwM2M TS 1.1
- Moved most of the Access Control mechanism logic from the access_control module (i.e., object implementation) to Anjay core
- Changed `anjay_execute_get_*()` error codes to ANJAY_ERR constants so that they can be safely propagated by DM handler callbacks
- Added generating notifications when Access Control object changes
- New revision of persistent format of the Server object implementation, with redesigned handling of Binding resource
- Improvements to demo client:
  - Notifications in Device object now work properly in the demo client
  - Demo client now supports different binding modes for different servers
  - Support for more resources in the Cellular Connectivity object
  - Support for Event Log object
  - Support for BinaryAppDataContainer object
- Added script and build target for finding unused code

### Improvements

- Improvements to logging:
  - Made it easier to compile Anjay without any logs
  - Made some log messages more descriptive
  - Tweaked log levels for better manageability
- More robust downloader module, including improvements to HTTP ETag handling
- Allowed Accept option in all incoming requests, for better interoperability with certain server implementations
- Removed internal scheduler implementation, migrated to avs_sched from avs_commons
- Made use of stack-allocated avs_persistence contexts
- Refactored handling of server connections and data in Security and Server objects to be more in line with OMA guidelines
- Refactored CMake scripts to make use of CMake 3 features
- Extracted parts of dm_core.c to separate files
- Improvements to internal symbol naming scheme
- Simplified a lot of internal APIs, including I/O contexts and DM path handling
- Various cleanups and improvements of Python-based integration test code

### Bugfixes

- Added missing protocol version in responses to Bootstrap Discover on an object path
- Various minor bugfixes

Also updates avs_commons to version 4.0.0, which includes the following changes:

### Breaking changes

- Removed ignoring context feature from avs_persistence
- Refactored error handling, introducing the new avs_error_t concept
- Renamed avs_stream_abstract_t to avs_stream_t
- Renamed avs_net_abstract_socket_t to avs_net_socket_t

### Features

- avs_net
  - Added support for Server Name Identification (D)TLS extension when using OpenSSL, and ability to enable or disable it explicitly
  - Added support for DTLS Connection ID extension if using a development version of mbed TLS that supports it
  - Added possibility to use custom mbed TLS entropy pool configuration
  - Added ability to configure (D)TLS ciphersuites
  - Added propagation of (D)TLS handshake alert codes to user code
  - Implemented accept() call for UDP sockets
  - Added avs_url_parse_lenient function and separate validation functions
- avs_stream
  - Added avs_stream_membuf_take_ownership function
  - Added avs_stream_membuf_reserve function
- avs_utils
  - Added avs_unhexlify function
- avs_algorithm
  - Refactored base64 to support alternate alphabets and padding settings
- avs_unit
  - Added support for and_then callbacks in mock sockets

### Improvements

- Made logs render "..." at the end if truncated
- Improved compatibility with various platforms, including Zephyr
- Improved structure of CMake stage configuration, removed unused definitions
- Reformatted entire codebase

### Bugfixes

- Added extern "C" clauses missing in some files, added regression testing for that, fixed some other C++ incompatibilities
- Fixed some improperly propagated error cases in HTTP client
- Fixed problems with avs_net sockets not working for localhost if no non-loopback network interfaces are available
- Fixed some potential NULL dereferences, assertion errors and various other fixes

## Anjay 1.16 (September 12th, 2019)

### Features

- Added anjay_fw_update_set_result API for changing Firmware Update Result at runtime

### Improvements

- Make Travis tests a bit faster by no longer using --track-origins=yes Valgrind argument, and also using pre-built Docker images

### Bugfixes

- Disabled stdin buffering in demo application. Fixes occasional hangs in Python tests
- Updated usages of deprecated `avs_persistence_*` functions
- Added mbedx509 to pymbedtls dependencies
- Fixed issues found by Coverity scan

## Anjay 1.15.5 (April 24th, 2019)

### Bug fixes

- Updated avs_commons to 3.10.0, which includes a fix that drastically (one order of magnitude in some cases) changes the result of `avs_coap_exchange_lifetime()`. Previously the results were not in line with RFC7252 requirements.

### Improvements

- The client will no longer wait indefinitely for Bootstrap Finish, but rather for an interval of at most EXCHANGE_LIFETIME seconds since last Bootstrap Interface operation.

## Anjay 1.15.4 (April 19th, 2019)

### Bugfixes

- Fixed bug that caused Anjay fw_update module `perform_upgrade` handler to be called more than once in some cases

### Improvements

- Anjay CMakeLists.txt does not use `${PROJECT_NAME}` anymore, improving compatibility with projects that include it as sources

## Anjay 1.15.3a (April 5th, 2019)

### Improvements

- Documented retransmission parameters configuration in chapter "Retransmissions, timeouts & response caching"

## Anjay 1.15.3 (April 3rd, 2019)

### Bugfixes

- Fixed some issues found by Coverity scan
- Upgraded avs_commons to version 3.9.1, which includes:
  - Fix of usage of select() on platforms that do not support poll()
  - Added new `AVS_RESCHED_*` APIs

## Anjay 1.15.2 (March 25th, 2019)

### Improvements

- Use https:// URI instead of git:// for avs_commons submodule. This allows fetching the submodule without setting up SSH keys. Fixes [#23](https://github.com/AVSystem/Anjay/issues/23)

## Anjay 1.15.1 (February 19th, 2019)

### Improvements

- Anjay will never attempt to send Register/Update from anjay_delete any more. It used to happen when that message was scheduled to be sent at a time that happened to pass between last anjay_sched_run and anjay_delete calls.

## Anjay 1.15 (February 14th, 2019)

### BREAKING CHANGES

- Updated avs_commons library to 3.9, which extracts an avs_stream_net library to break a dependency cycle between components. Applications that do not use CMake need to manually add libavs_stream_net.a to the linker command line.

### Bugfixes

- PUT/POST requests with an Accept: CoAP option are no longer rejected as invalid.
- Fixed a bug in HTTPS downloader that caused the download to hang indefinitely if the internal buffer of TLS socket is larger than 4KB and downloaded data length module TLS socket buffer size is larger than 4KB.
- Aborting HTTP(S) downloads no longer waits for the whole transfer to complete.

## Anjay 1.14.2 (January 29th, 2019)

### Bugfixes

- Fixed NULL pointer dereference in log messages displayed when an unknown attribute with no value is passed to Write-Attributes

## Anjay 1.14.1 (January 22nd, 2019)

### Features

- Added command line flag to the demo client that disables use of stdin

### Improvements

- Removed some code duplicated with avs_commons
- Fixed some minor issues found by scan-build 7
- Simplified flow of code for Register, Update and Request Bootstrap operations
- Reformatted example code

### Bugfixes

- Fixed a bug that prevented attempt to retry DTLS handshake after failed Request Bootstrap operation in some scenarios when Server-Initiated Bootstrap is enabled
- Fixed misleading, erroneous log message for when receiving CoAP messages time out
- Fixed the documentation URL test randomly failing on some machines

## Anjay 1.14.0 (December 4th, 2018)

### Improvements

- Added anjay_configuration_t::stored_notification_limit configuration option for limiting the maximum number of notifications stored when the client is offline.

## Anjay 1.13.1 (October 8th, 2018)

### Improvements

- Fixed compilation warnings caused by unused variables / mismatching printf format specifiers

## Anjay 1.13.0 (October 4th, 2018)

### Breaking changes

- anjay_configuration_t::max_icmp_failures field has been removed.
- Changed the way connection errors are handled. Connections are now NOT automatically retried in most of the cases. Please refer to the documentation (Advanced tutorial -> Network error handling) for a summary of the new semantics.

### Improvements

- Extensive refactor of the server connection handling subsystem.
- Added timeout for the documentation URL check.
- Prevented integration tests from running concurrently on Travis.

### Bugfixes

- Fixed behaviour when the attributes are set so that pmax < pmin.
- Fixed a bug that caused Discover operation on the Security object to erroneously work instead of causing 4.01 Unauthorized error as mandated by the spec.
- Fixed compilation warnings on various compilers.
- Upgraded avs_commons to version 3.8.2, which includes:
  - Fixes for proper propagation of avs_stream_close() errors.
  - Fixes for external library dependency checking.
  - Fixes for various compilation warnings.
  - Improved logs from the IP address stringification code.

## Anjay 1.12.1 (September 21st, 2018)

Update avs_commons to 3.8.1

## Anjay 1.12.0 (September 21st, 2018)

### Breaking changes

- Updated AvsCommons to 3.8.0, which requires CMake 3.4.0 or higher. This means that Anjay requires CMake 3.4.0 or higher as well.
- Running tests requires grequests (https://github.com/kennethreitz/grequests) now

### Features

- Allowed configuration of UDP DTLS Handshake transmission parameters by anjay_configuration_t::udp_dtls_hs_tx_params field
- Allowed configuration of firmware download CoAP transmission parameters by anjay_fw_update_handlers_t::get_coap_tx_params handler implemented by the user
- Added sequence diagrams for library operations in documentation chapter "4.4. A few notes on general usage"

### Improvements

- Reformatted the entire codebase with clang-format
- Added more tests verifying demo client's behavior in situations with network connectivity issues
- Explained in the demo application why file descriptors other than 0, 1, 2, are being closed

### Bugfixes

- Fixed the cause of "could not stringify socket address" error

## Anjay 1.11.0 (September 4th, 2018)

### Breaking changes

- `Removed ANJAY_BINDING_*` constants. Whenever used, they should now be replaced with plain c-strings, as follows:
  * `ANJAY_BINDING_U` -> `"U"`,
  * `ANJAY_BINDING_S` -> `"S"`,
  * `ANJAY_BINDING_US` -> `"US"`,
  * `ANJAY_BINDING_UQ` -> `"UQ"`,
  * `ANJAY_BINDING_SQ` -> `"SQ"`,
  * `ANJAY_BINDING_UQS` -> `"UQS"`,
  * `ANJAY_BINDING_NONE` -> `""`

### Features

- Implemented anjay_attr_storage_purge(), to allow cleaning up Attribute Storage data without recreating a whole client instance
- Implemented anjay_access_control_purge(), anjay_access_control_is_modified(), to allow better control over persistence
- Updated avs_commons to version 3.7.1

### Bugfixes

- Fixed implementation of bytes resources in demo test object code
- Added missing header in attr_storage.h

### Improvements

- Added support for multiple object versions in lwm2m_object_registry.py script
- Added some previously missing optional packages to README.md, required to run integration tests
- Improved performance of integration tests
- Improved documentation of internal server-related APIs
- Improved unit tests API, specifically added macros that help building CoAP messages without the knowledge of exact packet encoding

## Anjay 1.10.4 (July 30th, 2018)

### Features

- Added a configuration option that allows disabling Server-Initiated Bootstrap

### Bugfixes

- Very short HTTP downloads now do not hang forever when the server does not close the TCP connection

### Improvements

- Refactored management of bootstrap backoff state
- Add tests for client behavior after receiving 4.03 Forbidden in response to Register request

## Anjay 1.10.3a (July 19th, 2018)

### Fixes

- Fixed Travis build

## Anjay 1.10.3 (July 19th, 2018)

### Fixes

- Fixed warning about uninitialized retval when compiling in SW4STM32

### Improvements

- Added validation of URLs in documentation
- Added multiple test cases

## Anjay 1.10.2 (July 10th, 2018)

### Features

- Updated AvsComons to version 3.6.2 which includes:
  * a more restrictive approach to symbols from POSIX or C standard library that should not be used in embedded environments
  * a fix of compilation on ARMCC
  * a fix of compile time warning on IAR

### Fixes

- Fixed client behavior when received a 4.03 Forbidden on LwM2M Register
- Fixed outdated reference to the LwM2M Specification in the documentation

### Improvements

- Added more tests verifying client behavior in different scenarios

## Anjay 1.10.1 (June 29th, 2018)

### Fixes

- Updated avs_commons to 3.6.1 - fixes compatibility issues in tests.

## Anjay 1.10.0 (June 28th, 2018)

### Features

- Updated avs_commons to 3.6.0, which includes:
  * an abstraction layer over allocator routines, making it possible for the user to provide custom allocation/deallocation functions to be used by AvsCommons,
  * removal of ``AVS_LIST_CONFIG_ALLOC/FREE`` (they are now replaced with calls to ``avs_calloc()`` and ``avs_free()`` respectively),
  * removal of use of all ``time()`` calls,
  * removal of use of variable length array language feature,
  * default socket implementation refactor to use a nonblocking socket API,
  * ``avs_compat_threading`` module, implementing necessary synchronization primitives used across AvsCommons such as mutexes,
  * ``avs_cleanup_global_state()`` method, allowing to (optionally) free any global state implicitly instantiated in AvsCommons,
  * various compatibility fixes for FreeBSD.

- Prevented ``anjay_schedule_reconnect()`` from sending Updates when they are not necessary,

- Introduced an API (``anjay_get_socket_entries()``) allowing to obtain different kinds of sockets used by Anjay.

### Improvements

- Removed all uses of ``malloc()/calloc()/realloc()/free()`` in favor of AvsCommons' memory layer abstraction,
- Removed all occurrences of ``time()``,
- Removed all uses of variable length arrays,
- Improved non-GNU compilers compatibility,
- Fixed multiple typos in documentation,
- Fixed LwM2M-level error reporting for some LwM2M requests with unexpected payloads/content-formats (sane error codes are now returned instead of Internal Server Error),
- Various compatibility fixes for FreeBSD.

### Fixes

- Made bind address family depend on resolved numeric address rather than domain name,
- Fixed project compilation when ``WITH_AVS_PERSISTENCE`` is disabled.

## Anjay 1.9.3 (May 29th, 2018)

### Improvements

- Updated avs_commons to 3.4.3
- AVS_ASSERT and AVS_UNREACHABLE macros are now used for assertions that contain string literals. This prevents some compilers from emitting warnings about constant expressions being used in asserts.

## Anjay 1.9.2 (May 28th, 2018)

### Features

- Updated avs_commons to 3.4.2 including various compatibility improvements
- Added preliminary Windows support

### Bugfixes

- Fixed many different casts between incompatible function types found by gcc 8.1
- Fixed compile errors caused by some (perfectly valid) CMake option configurations

### Improvements

- Improved documentation of anjay_fw_update_perform_upgrade_t
- Improved compatibility with compilers without typeof() support

## Anjay 1.9.1 (May 17th, 2018)

### Bugfixes

- Fixed searching for scan-build ("make analyze" target) on Ubuntu 18.04
- Prevented sending superfluous notifications before cleaning up the library

Also updates avs_commons to version 3.4.1, which includes the following changes:

### Bugfixes

- Fixed bug in avs_http that prevented digest authentication from working
- Fixed conditional compilation bugs in avs_net that made it impossible to disable certain features
- Fixed bugs in avs_net unit tests that prevented them from passing on systems without JDK installed and when ran as root

### Improvements

- Simplified TLS session persistence logic (removed dependency on mbed TLS session cache)
- Fixed compilation warnings on mbed TLS >= 2.7
- Worked around false positive warnings from scan-build 6.0

## Anjay 1.9.0 (May 11th, 2018)

### Features

* anjay_notify_instances_changed() are now automatically called when manipulating pre-implemented Security and Server objects via module API
* (commercial version only) Added support for persistence of server registration and notification state, designed for devices with aggressive power saving
* (commercial version only) Added minimal CoAP file server to the command-line test server application

### Improvements

* BREAKING API CHANGE: Security and Server object implementation modules no longer expose the anjay_dm_object_def_t pointer directly.
* anjay_schedule_reconnect() now also reconnects downloads started using anjay_download()
* Notifications with non-success message codes are now always sent as Confirmable messages to ensure consistency with server-side state
* Integration tests now can be easily launched under rr for easier debugging
* Added various informative log messages
* Moved persistence subsystem to avs_commons and migrated to it
* Fixed various compilation warnings and compatibility with different compilers
* Major internal codebase refactoring, including:
  * Saner scheduler function signatures
  * Changed registration expiration time to use realtime clock instead of the monotonic one, which improves compatibility with sleep mode scenarios
  * Better hermetization of Observe handling implementation
  * Simplification and better hermetization of server connection handling
  * Reorganization of Registration Interface implementation
* (commercial version only) More flexible management of commercial features during packaging

Also updates avs_commons to version 3.4.0, which includes the following changes:

### Features

* Moved persistence subsystem from Anjay and improved upon it:
  * Added support for persisting additional integer types
  * Added support for persisting containers with variable size elements
  * Added ability to check the type of persistence context

### Improvements

* BREAKING API CHANGE: Changed TLS session resumption API so that it is now serialized to and deserialized from user-provided buffer
* BREAKING API CHANGE: Simplified certificate and key configuration API
  * Note that this change dropped support for some libraries that implement "fake" OpenSSL API
* Refactored avs_log() so that compiler will always parse TRACE-level logs, even if code generation for them is disabled
* Fixed various compilation warnings and compatibility with different compilers
* Fixed warnings when compiling with mbed TLS 2.3 and newer

### Bugfixes

* Fixed critical bugs in CoAP option handling:
  * Potential integer overflow
  * Erroneous operation on big-endian machines
* Added various missing NULL checks

## Anjay 1.8.2 (March 14th, 2018)

### Improvements

- Added X.509 certificate support to pymbedtls Python module.
- Made `BINDING_MODE_AS_STR` `const`.
- Changed type of the buffer size argument of `anjay_execute_get_arg_value` to unsigned size_t.
- Added a note on using LwIP socket integration layer to "Porting guide for non-POSIX platforms".
- Added debug logs in instance validators for Security and Server LwM2M objects.
- Added proper notifications for "Last Execute Arguments" resource of the Test Object in demo client.
- Disabled Coverity scan on Travis. This avoids marking the build as failing despite all tests passing - Coverity service is "down for maintenance" since 2018-02-20, and there seems to be no information on when will it be up again.

### Bugfixes

- Fixed `anjay_schedule_reconnect` behavior when called after the client gives up on reaching a LwM2M server. Previously, only a single reconnection attempt was attempted in such case, regardless of the `max_icmp_failures` configuration option.
- Fixed compilation errors on compilers that do not support typeof.

## Anjay 1.8.1 (February 28th, 2018)

### Bugfixes

- Fixed infinite loop of Register retransmissions when LwM2M Server ignores Updates and Client attempts to re-register
- Fixed nested links in README.md

## Anjay 1.8.0 (February 21st, 2018)

### Features

- Added get_security_info() handler to fw_update module, enabling configuration of security information for PULL-mode downloads over encrypted channels
- Added anjay_fw_update_load_security_from_dm() which allows to match security information from the Security object based on URI

### Bugfixes

- fw_update module will no longer connect to any HTTPS or CoAPS URI without authentication

## Anjay 1.7.3 (February 16th, 2018)

### Features

- anjay_codegen is now able to generate C++ code

### Improvements

- Updated timeouts in integration tests which should improve test result stability
- Added log when security mode does not match server URI

## Anjay 1.7.2 (February 12th, 2018)

### Bugfixes

- Fix anjay_all_connections_failed(). It is no longer returning true if no LwM2M Servers are configured.

## Anjay 1.7.1 (February 12th, 2018)

### Bugfixes

- Skip retransmission loop in case of DTLS handshake timeout. DTLS packet retransmissions are handled within avs_net_socket_connect anyway, so there is no point in applying yet another exponential backoff loop.

## Anjay 1.7.0 (February 12th, 2018)

### Breaking changes

- Reverted anjay_server_unreachable_handler_t as it has been found that using the handler correctly is close to being impossible.

- Anjay no longer attempts to reach LwM2M Servers indefinitely. Maximum number of retries is now configured via anjay_configuration_t::max_icmp_failures, and by default is set to 7.

### Features

- Introduced anjay_all_connections_failed() method, allowing the user to check if Anjay already gave up on trying to reach LwM2M Servers.

### Improvements

- Downgrade log level in _anjay_dm_foreach_instance() to TRACE.

## Anjay 1.6.1 (January 29th, 2018)

### Features

- Added anjay_server_unreachable_handler_t, that may be implemented by the user to control behavior of Anjay when it failed to connect to the LwM2M Server
- Added new demo command line option "--server-unreachable-action" to be able to present the aforementioned handler in action
- Added new demo command "enable-server" to enable Server of specified SSID
- Added anjay_disable_server_with_timeout()

### Improvements

- Improved compatibility with CMake 2.8.12

### Bugfixes

- Fixed Update interval: when lifetime is larger than `2*MAX_TRANSMIT_WAIT`, Update is now sent at `lifetime-MAX_TRANSMIT_WAIT` instead of `lifetime/2`
- Fixed demo command line parsing functions
- Fixed Travis problems and configuration on CentOS

## Anjay 1.6.0 (January 8th, 2018)

### Breaking changes

- Replaced time_t with int32_t for period Attributes; fixes compatibility with platforms that have unsigned time_t

### Improvements

- Removed useless symlinks that caused problems on Windows
- Fixed usage of errno constants that are defined by avs_commons compatibility layer; fixes compatibility with platforms that don't declare sane errno constants
- Improved compatibility with CMake 2.8 and CentOS

### Other

- anjay_persistence_time() is now deprecated

## Anjay 1.5.2 (December 11th, 2017)

### Bugfixes

- Fixed flow of flushing unsent notifications
- Bug fixes in avs_commons, including:
  - Fixed undefined behavior in CoAP message cache
  - Fixed compatibility with compilers that don't support either stdatomic.h or GCC-style `__sync_*` builtins
  - Prevented CoAP back-off timer randomization from occasionally using negative numbers
  - Fixed minor error handling problems
  - Fixed link commands for TinyDTLS interoperability

### Improvements

- Added WITH_TEST CMake flag
- Improved compatibility with BSD operating systems
- Improvements in avs_commons, including:
  - Fixed interoperability with HTTP servers that unexpectedly close connection

## Anjay 1.5.1 (November 27th, 2017)

### Features

- Support HTTP download resumption

### Bugfixes

- Fix some race conditions in integration tests that revealed themselves on slow machines
- Fix anjay_download() retrying download forever even in case of terminal failures
- Fix Firmware Update resetting by a null-byte Write
- Stop scheduling useless LwM2M Updates to the Bootstrap Server

### Improvements

- Test Firmware Update "not enough storage" scenario over CoAP
- Test Firmware Update "connection lost during download" scenario over CoAP

## Anjay 1.5.0 (November 20th, 2017)

### Features

- Extracted Firmware Update logic to a separate module so that the end user have to implement device-specific firmware updating logic only
- Implemented API for firmware update resumption
- Implemented stub of the portfolio object (in demo client) required for the OMA TestFest 2017
- Added support for DTLS session resumption as well as register-after-reconnect semantics
- Added object versioning support
- Added support for LwM2M Server URIs with Uri-Path and Uri-Query

### Bugfixes

- Fixed travis builds on macOS
- Fixed a few misleading statements in the documentation
- Fixed anjay_codegen.py handling of Multiple Instance Resources
- Fixed Content-Format for responses on Bootstrap Discover request
- Fixed Write (replace) on Device object instance in demo client

### Improvements

- Added more tests covering OMA TestFest 2017 test cases
- Allowed configuring Security/Server IIDs from command line in demo
- Allowed Bootstrap Delete on "/"
- Added support for re-bootstrapping after failed registrations
- Added anjay_server/security_object_is_modified simiar to anjay_attr_storage_is_modified
- Updated porting guide
- Replaced Internal Server Error responses with more specific error codes in a few places

### Other

- Relaxed validator of Update location path, due to specification being unclear (see: https://github.com/OpenMobileAlliance/OMA_LwM2M_for_Developers/issues/230)

## Anjay 1.4.1 (October 16th, 2017)

### Features

- Added CMake option `WITH_STATIC_DEPS_LINKED` that forces direct linkage of the library dependencies into the final target library
- Migrated to a new time API implemented in avs_commons
- Removed dependency on wget completely and used built-in downloader instead

### Bugfixes

- Fixed symbol visibility checks

### Improvements

- Renamed a few files to improve compatibility with various IDEs that do not handle files with non-unique naming across the entire project
- Lowered severity of some log messages that were actually not that critical
- Published example output from anjay_codegen.py script in the documentation

## Anjay 1.4.0 (September 8th, 2017)

### Features

- New tools: lwm2m_object_registry.py and anjay_codegen.py, that allow automatic generation of object implementation stubs from LwM2M object definition XMLs
- anjay_download() now supports HTTP(S), using the client from avs_commons
- New APIs for querying Anjay's network traffic statistics
- New APIs in attr_storage for direct attribute manipulation:
  - anjay_attr_storage_set_object_attrs()
  - anjay_attr_storage_set_instance_attrs()
  - anjay_attr_storage_set_resource_attrs()
- CoAP implementation base has been refactored and moved to avs_commons, so that it can now be used standalone; Anjay code has been refactored accordingly

### Bugfixes

- Fixed a bug that prevented anjay_get_string() from working as documented when the buffer was too short
- Fixed conformance with RFC 7252 when sending error responses on observed resources (previously the Observe header was erroneously included)
- Fixed various minor bugs found through static code analysis and compilation on various platforms

### Improvements

- POSIX dependencies are now better isolated to ease porting onto non-POSIX platforms
- Added more documentation, including:
  - New tutorial page (BT4) with general notes on library usage
  - Porting guide for non-POSIX platforms
- Removed some superfluous log messages

## Anjay 1.3.3 (July 27th, 2017)

### Features

- Implemented anjay_download() API for asynchronous CoAP(S) downloads
- Added anjay_download example code
- Added support for CoAP firmware download in demo application

### Bugfixes

- Fixed Register/Update transport when changing Binding
- Fixed lt/gt/st semantics according to https://github.com/OpenMobileAlliance/OMA_LwM2M_for_Developers/issues/191
- Fixed handling of unrelated BLOCK2 requests during a block-wise Read
- Disallowed Write-Attributes requests if the server does not have Read access rights
- Fixed build instructions for OS X in README

### Improvements

- Added packet capture in Python tests
- Added compilation instructions for Android
- Made missing scan-build a fatal error if static analysis was enabled with a CMake flag
- Integrated Coverity scan with Travis build
- Allowed configuration of CoAP transmission parameters in anjay_new()

## Anjay 1.3.2 (July 13th, 2017)

### Features

- Added custom Attribute "con" that may be used to enforce sending Confirmable Notifications for observed entities.

- Added new chapter of the Tutorial about Executable Resources.

- Added documentation subsection about relation LwM2M Discover and Attribute Storage.

### Improvements

- Removed dependency to Boost.Python in tests, and migrated to pybind11 instead (included as a submodule).

- Implemented sending Update after Lifetime Resource has changed (https://github.com/OpenMobileAlliance/OMA_LwM2M_for_Developers/issues/185)

- Implemented proper Bootstrap Server Account purge logic (https://github.com/OpenMobileAlliance/OMA_LwM2M_for_Developers/issues/195)

- Improved handling of CoAP Ping messages - they no longer produce error messages in the logs.

- Documented `ANJAY_ERR_*` constants.

- Added more NULL-assertions in the example demo client.

- Enabled scan-build in make check by default.

- Bumped all copyright years.

### Bugfixes

- Fixed dead URLs in the documentation.

- Fixed segfault when Bootstrap Discover was performed on a non-existing Object.

- Fixed various minor issues found by the static analysis tools.

- Fixed various compilation issues when compiled on older Android platforms.

## Anjay 1.3.1 (June 22nd, 2017)

### Features

- Added `confirmable_notifications` field to `anjay_configuration_t`, enabling the client to only send LwM2M Notify as CoAP Confirmable messages.
- Added retransmission detection using message cache with fixed size, configurable at library initialization.
- Added Custom Object/Notifications tutorial with example client.
- Added documentation page explaining message cache purpose and usage.

### Improvements

- Added support for Write on Instance with superfluous TLV instance header. Anjay used to reject such requests as malformed.
- Implemented ETS test 204 (Read with Accept: JSON).
- Made attribute parsing stricter. Unknown or duplicate attributes now cause Bad Request responses
- Splitted `anjay.h` header into smaller ones. Note: `anjay.h` now includes all other headers, so no changes to user code are required.

### Bugfixes

- Fixed problem with duplicate request aborting block-wise Read responses.
- Prevented tests from failing if Sphinx is not installed.
- Fixed ConnectivityMonitoring.APN type to Multiple Resource. Fixes issue #10.
- Fixed semantics of `lt` and `gt` attributes to match draft-ietf-core-dynlink document.
- Fixed build issue when configuring build with -DWITH_BOOTSTRAP=OFF CMake option.
- Fixed compilation warnings in relase builds.

## Anjay 1.3.0 (May 26th, 2017)

### Features

* Added initial output-only support for JSON Content-Format
* Added support for SMS-related Resources in security module
* Refactored code to facilitate support for SMS Binding
  * Actual SMS Binding support now implemented in the commercial version

### Improvements

* BREAKING API CHANGE: Replaced rid_bound and resource_supported handler with statically declared list of supported resources
* Improved handling of DTLS backends in build system
* 5.03 Service Unavailable is now sent instead of Reset when an unexpected request arrives while waiting for a response to Register or Update
* Improvements in demo client:
  * Fixed Firmware Update object state machine
  * Added default Access Control entries

### Bugfixes

* Fixed sending errnoeus 2.31 Continue if the last block payload chunk trigerred an error
* Relaxed invariants for Client-Initiated Bootstrap as per https://github.com/OpenMobileAlliance/OMA_LwM2M_for_Developers/issues/164
* Prevented sending Object Instance list in Update messages if only changes are in the Security object
* Fixed various bugs in access_control module

### Other

* Fixed in-code version numbers

## Anjay 1.2.1 (April 10th, 2017)

### Features

* Added automatic Update after object (de)register

### Bugfix

* Fixed potential access violation in anjay_attr_storage_restore()

## Anjay 1.2.0 (April 3rd, 2017)

### Features

* Added new API: anjay_unregister_object()

### Improvements

* Added new constant: ANJAY_ERR_SERVICE_UNAVAILABLE
* Made documentation linter work better when the MD5 file is broken
* Refactored anjay_dm_object_def_t, introduced anjay_dm_handlers_t
* Refactored attr_storage so that object wrapping is no longer necessary
* Fixed Travis configuration and added macOS build
* Refactored access_control implementation, simplified API
* Removed the on_register object handler

## Anjay 1.1.1 (March 14th, 2017)

### Features

* Added API to specify configuration for CoAP sockets, including overriding path MTU
* Added support for new DTLS backend: tinydtls

### Improvements

* When CoAP logic is blocked on BLOCK processing, 5.03 is now sent as response to unrelated requests
* Anjay can now be compiled and run on macOS
* Updated compliance to the released 1.0 specification
* Added actual reconnection of sockets when using queue mode

### Bugfixes

* Fixes to various issues found using static analysis
* Fixed compliance issues with handling Bootstrap-Server Account Timeout
* Removed automatic instantiation of Access Control object instances, as per https://github.com/OpenMobileAlliance/OMA_LwM2M_for_Developers/issues/192
* Fixed handling of integration test dependencies in CMake files
* Workaroud for bug in mbed TLS, see https://github.com/Mbed-TLS/mbedtls/issues/843

## Anjay 1.1.0 (February 22nd, 2017)

### Features

* Demo client loads certificates from files
* Added Bootstrap Awareness tutorial
* Added Access Control tutorial
* Added DTLS tutorial
* Refactored Attribute handling to meet current LwM2M standard requirements
* Improved out of source build support
* Improved test coverage

### Bugfixes

* Fixed coverage script
* Fixed fuzz tests compilation & running on some configurations
* Fixed mbedTLS detection in integration tests
* Fixed a case where mismatched Resource ID was accepted in the TLV payload
* Fixed compilation of tests under gcc-4.6
* Fixed detection of the required python version

### Other

* Added missing license headers
* Lowered loglevel of some less important messages
* Refactored some of the macros and replaced them with real C code
* Replaced all "LWM2M" with "LwM2M"
* Other minor fixes

## Anjay 1.0.0 (February 8th, 2017)

Initial release.
