#ifndef DEMO_NIDD_DEMO_DRIVER_H
#define DEMO_NIDD_DEMO_DRIVER_H

#include <anjay/core.h>
#include <anjay/nidd.h>

/**
 * Simple NIDD driver that connects to the PTY of a modem device, responsible
 * for NIDD connectivity.
 *
 * @param modem_device  Path to the modem pseudo-terminal device, e.g.
 *                      "/dev/pts/1".
 *
 * @returns pointer to a newly created NIDD driver.
 */
anjay_nidd_driver_t **demo_nidd_driver_create(const char *modem_device);

void demo_nidd_driver_cleanup(anjay_nidd_driver_t **driver);

#endif /* DEMO_NIDD_DEMO_DRIVER_H */
