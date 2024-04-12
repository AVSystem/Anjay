# Changelog

## Anjay 4.0.0-alpha.2 (April 12th, 2024)

### Features

* Added PlainText, Opaque, CBOR, SenML CBOR, and LwM2M CBOR input formats
* Added PlainText, Opaque, LwM2M CBOR output formats
* New building system based on CMake
* Bootstrap-Delete and Bootstrap-Write are now fully supported in a static data model
* Full implementation of Security (/0), Server (/1), and Device (/3) objects
* Added PlatformIO implementation
* Added time API `anj_time.h`
* Added new examples with firmware update, SEND operation, and basic Bootstrap support

### Bugfixes

* Fixed bugs in a static data model
* Removed all clang compilation warnings
* Fixed error handling in `sdm_process`

### Other

* Added some macros to make it easier to use the static data model API
* The `sdm_res_t` stores `res_value` as a pointer to reduce the size of the structure
* Slightly modified the logic of the CREATE operation in the static data model
* `anjay_lite` and examples based on it have been removed and replaced by full implementations of the objects and custom event loop in examples
* Net API has been moved to `anj_net.h`
* Removed unused compilation flags
* Improved test coverage
* Added missing license headers
* Other minor fixes

## Anjay 4.0.0-alpha.1 (January 31st, 2024)

Initial release.
