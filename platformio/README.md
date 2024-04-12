# Anjay PlatformIO integration

To include Anjay in your PlatformIO project, clone this repository into the
`<pio_project_directory>/lib` directory. PlatformIO will automatically build
Anjay as a static library and will allow you to include Anjay's files inside
your project.

## Examples

In the `platformio/examples` directory, you will find examples of using Anjay
PlatformIO integration. Currently supported platforms are:

* Arduino Nano 33 IoT
* ESP32 DevKitC

(for more information, see the
`platformio/examples/<example_name>/platformio.ini` file.).

## Changing the default config of Anjay

Anjay PlatformIO integration comes with a "ready-to-go" default config
included in the `platformio/config` directory. To overwrite this custom config,
a few steps need to be performed:

1. Create a `<pio_project_directory>/include/custom_config` directory

1. Copy the file you are interested in from the `platformio/config/<module>`
directory (`platformio/config/<module>/<module>_config_platformio_default.h`)
into the `<pio_project_directory>/include/custom_config/<module>_config.h` file

1. In the `<pio_project_directory>/platformio.ini` file, inside the
`build_flags` option, add the
`-DANJAY_<MODULE>_CONFIG_PLATFORMIO_OVERRIDE_FILE=\"custom_config/<module>_config.h\"`
option

1. In the `<pio_project_directory>/platformio.ini` file, inside the
`build_flags` option, add the `-Iinclude/` option

Then make changes in the
`<pio_project_directory>/include/custom_config/<module>_config.h` file you are
interested in. Those changes will be applied to Anjay build configuration.
