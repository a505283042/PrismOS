/* SPDX-License-Identifier: MIT */
#include "driver/usb_serial_jtag.h"

/* IDF 6.1's stdio REPL installs a fixed 256-byte RX ring. Aster's binary
 * transfer sends a complete 512-byte frame before waiting for an ACK.
 * Override the installation for this board without changing the IDF checkout
 * or the other boards' console backends. Keep larger caller buffers intact. */
esp_err_t __real_usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t *config);
esp_err_t __wrap_usb_serial_jtag_driver_install(usb_serial_jtag_driver_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    usb_serial_jtag_driver_config_t adjusted = *config;
    if (adjusted.rx_buffer_size < 2048) adjusted.rx_buffer_size = 2048;
    return __real_usb_serial_jtag_driver_install(&adjusted);
}
