/**
 * @file sa818_at.h
 * @brief SA818 AT Command Interface
 *
 * Provides UART-based AT command protocol implementation for
 * configuring SA818 radio module parameters including frequency,
 * CTCSS codes, filters, and volume control.
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#ifndef ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_SA818_AT_H_
#define ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_SA818_AT_H_

#include <sa818/sa818.h>
#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief AT Command subsystem for SA818
 *
 * Handles UART communication and AT command protocol
 * for configuring the SA818 radio module.
 */

/**
 * @brief Send raw AT command and receive response
 *
 * @param dev SA818 device
 * @param cmd Command string to send
 * @param response Buffer for response (can be NULL)
 * @param response_len Length of response buffer
 * @param timeout_ms Timeout in milliseconds
 * @return SA818_OK on success, error code on failure
 */
[[nodiscard]] enum sa818_result sa818_at_send_command(const struct device *dev, const char *cmd, char *response, size_t response_len, uint32_t timeout_ms);

/**
 * @brief Configure radio group (frequency, CTCSS, squelch)
 *
 * @param dev SA818 device
 * @param bandwidth Bandwidth: 0=12.5kHz, 1=25kHz
 * @param freq_tx TX frequency in MHz (e.g. 145.500)
 * @param freq_rx RX frequency in MHz (e.g. 145.500)
 * @param ctcss_tx TX CTCSS code (0000-0038 for off/tones)
 * @param squelch Squelch level (0-8)
 * @param ctcss_rx RX CTCSS code
 * @return SA818_OK on success, error code on failure
 */
[[nodiscard]] enum sa818_result sa818_at_set_group(const struct device *dev, uint8_t bandwidth, float freq_tx, float freq_rx, uint16_t ctcss_tx,
                                                   uint8_t squelch, uint16_t ctcss_rx);

/**
 * @brief Set volume level
 *
 * @param dev SA818 device
 * @param volume Volume level (1-8)
 * @return SA818_OK on success, error code on failure
 */
[[nodiscard]] enum sa818_result sa818_at_set_volume(const struct device *dev, uint8_t volume);

/**
 * @brief Configure audio filters
 *
 * @param dev SA818 device
 * @param pre_emphasis Pre-emphasis enable
 * @param high_pass High-pass filter enable
 * @param low_pass Low-pass filter enable
 * @return SA818_OK on success, error code on failure
 */
[[nodiscard]] enum sa818_result sa818_at_set_filters(const struct device *dev, bool pre_emphasis, bool high_pass, bool low_pass);

/**
 * @brief Read RSSI (signal strength)
 *
 * @param dev SA818 device
 * @param rssi Pointer to store RSSI value
 * @return SA818_OK on success, error code on failure
 */
[[nodiscard]] enum sa818_result sa818_at_read_rssi(const struct device *dev, uint8_t *rssi);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_SA818_AT_H_ */
