/**
 * @file sa818.h
 * @brief SA818/SA818S VHF/UHF FM Transceiver Module Driver
 *
 * This driver provides control interface for the NiceRF SA818/SA818S
 * radio transceiver modules (VHF 134-174MHz, UHF 400-480MHz).
 *
 * Features:
 * - Power control (on/off)
 * - PTT (Push-To-Talk) control
 * - TX power level control (high/low)
 * - Squelch monitoring
 * - AT command interface for frequency/CTCSS configuration
 * - Audio subsystem integration
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#ifndef ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_SA818_H_
#define ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_SA818_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup sa818_core SA818 Core API
 * @brief Core control functions for SA818 radio module
 * @{
 */

/**
 * @brief SA818 API result codes
 *
 * All SA818 driver functions return these status codes to indicate
 * success or specific error conditions.
 */
enum sa818_result {
  SA818_OK = 0,                    /**< Operation successful */
  SA818_ERROR_INVALID_DEVICE = -1, /**< Invalid device pointer or NULL */
  SA818_ERROR_NOT_READY = -2,      /**< Device not initialized or ready */
  SA818_ERROR_INVALID_PARAM = -3,  /**< Invalid parameter value */
  SA818_ERROR_GPIO = -4,           /**< GPIO operation failed */
  SA818_ERROR_UART = -5,           /**< UART communication error */
  SA818_ERROR_TIMEOUT = -6,        /**< Operation timed out */
  SA818_ERROR_AT_COMMAND = -7,     /**< AT command failed or invalid response */
  SA818_ERROR_ADC = -8,            /**< ADC operation failed */
  SA818_ERROR_DAC = -9,            /**< DAC operation failed */
  SA818_ERROR_NO_RESPONSE = -10,   /**< No response from module */
};

/**
 * @brief Device power states
 *
 * Controls the main power state of the SA818 module via PD (Power Down) pin.
 */
enum sa818_device_power {
  SA818_DEVICE_OFF = 0, /**< Module powered off (PD=HIGH) */
  SA818_DEVICE_ON = 1,  /**< Module powered on (PD=LOW) */
};

/**
 * @brief Set device power state
 *
 * Controls the module's power state via the nPOWER_DOWN GPIO pin.
 * When powered on, the module requires ~100ms to initialize before
 * AT commands can be sent.
 *
 * @param dev Pointer to the SA818 device structure
 * @param power_state Desired power state (ON or OFF)
 *
 * @return SA818_OK on success
 * @return SA818_ERROR_INVALID_DEVICE if device pointer is invalid
 * @return SA818_ERROR_GPIO if GPIO operation failed
 *
 * @note After power-on, wait for module initialization before sending commands
 */
enum sa818_result sa818_set_power(const struct device *dev, enum sa818_device_power power_state);

/**
 * @brief PTT (Push-To-Talk) states
 *
 * Controls transmit/receive mode of the radio module.
 */
enum sa818_ptt_state {
  SA818_PTT_OFF = 0, /**< Receive mode (PTT pin LOW) */
  SA818_PTT_ON = 1,  /**< Transmit mode (PTT pin HIGH) */
};

/**
 * @brief Set PTT (Push-To-Talk) state
 *
 * Switches the module between receive and transmit modes.
 * In transmit mode, the module will transmit on the configured frequency.
 *
 * @param dev Pointer to the SA818 device structure
 * @param ptt_state Desired PTT state (ON for TX, OFF for RX)
 *
 * @return SA818_OK on success
 * @return SA818_ERROR_INVALID_DEVICE if device pointer is invalid
 * @return SA818_ERROR_GPIO if GPIO operation failed
 *
 * @note TX enable delay (configured in device tree) is applied when entering TX mode
 * @warning Ensure antenna is connected before transmitting
 */
enum sa818_result sa818_set_ptt(const struct device *dev, enum sa818_ptt_state ptt_state);

/**
 * @brief RF output power levels
 *
 * Controls the RF output power via H/L pin.
 * Actual power output depends on module variant:
 * - SA818-V (VHF): LOW=0.5W, HIGH=1W
 * - SA818S-V (VHF): LOW=0.5W, HIGH=1.5W
 */
enum sa818_power_level {
  SA818_POWER_LOW = 0,  /**< Low power output (PD=LOW) */
  SA818_POWER_HIGH = 1, /**< High power output (PD=HIGH) */
};

/**
 * @brief Set RF output power level
 *
 * Controls the transmit power via the H_L GPIO pin.
 * Does not affect receive mode.
 *
 * @param dev Pointer to the SA818 device structure
 * @param power_level Desired power level (LOW or HIGH)
 *
 * @return SA818_OK on success
 * @return SA818_ERROR_INVALID_DEVICE if device pointer is invalid
 * @return SA818_ERROR_GPIO if GPIO operation failed
 *
 * @note Power level only affects TX mode, not RX mode
 */
enum sa818_result sa818_set_power_level(const struct device *dev, enum sa818_power_level power_level);

/**
 * @brief Squelch states
 *
 * Indicates whether the squelch is open (no carrier) or closed (carrier detected).
 * The squelch threshold is configured via AT commands.
 */
enum sa818_squelch_state {
  SA818_SQUELCH_CLOSED = 0, /**< Carrier detected (signal present, SQL pin LOW) */
  SA818_SQUELCH_OPEN = 1,   /**< No carrier detected (squelch open, SQL pin HIGH) */
};

/**
 * @brief Get squelch status
 *
 * Reads the squelch (SQL) pin to determine if a carrier signal
 * is being received. Squelch threshold is configured via AT commands.
 *
 * @param dev Pointer to the SA818 device structure
 *
 * @return SA818_SQUELCH_OPEN if no carrier detected
 * @return SA818_SQUELCH_CLOSED if carrier detected
 *
 * @note SQL pin is active HIGH when squelch is open (no signal)
 */
enum sa818_squelch_state sa818_get_squelch(const struct device *dev);

/**
 * @brief Device status structure
 *
 * Contains the current state of all controllable parameters
 * and monitored signals.
 */
struct sa818_status {
  enum sa818_device_power device_power;   /**< Current power state */
  enum sa818_ptt_state ptt_state;         /**< Current PTT state */
  enum sa818_power_level power_level;     /**< Current TX power level */
  enum sa818_squelch_state squelch_state; /**< Current squelch state */
};

/**
 * @brief Get current device status
 *
 * Retrieves the current state of all device parameters in a single call.
 * Status includes power state, PTT state, power level, and squelch status.
 *
 * @param dev Pointer to the SA818 device structure
 *
 * @return Structure containing current device status
 *
 * @note This function is thread-safe (protected by mutex)
 */
struct sa818_status sa818_get_status(const struct device *dev);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_SA818_H_ */
