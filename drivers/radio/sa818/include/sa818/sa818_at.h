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
 * @brief SA818 bandwidth configuration
 */
enum sa818_bandwidth {
  SA818_BW_12_5_KHZ = 0, /**< 12.5 kHz bandwidth (narrow) */
  SA818_BW_25_KHZ = 1    /**< 25 kHz bandwidth (wide) */
};

/**
 * @brief SA818 squelch level (0-8, higher = less sensitive)
 */
enum sa818_squelch_level {
  SA818_SQL_LEVEL_0 = 0, /**< Squelch level 0 (most sensitive) */
  SA818_SQL_LEVEL_1 = 1,
  SA818_SQL_LEVEL_2 = 2,
  SA818_SQL_LEVEL_3 = 3,
  SA818_SQL_LEVEL_4 = 4, /**< Default level */
  SA818_SQL_LEVEL_5 = 5,
  SA818_SQL_LEVEL_6 = 6,
  SA818_SQL_LEVEL_7 = 7,
  SA818_SQL_LEVEL_8 = 8 /**< Tightest squelch (least sensitive) */
};

/**
 * @brief SA818 CTCSS/DCS tone codes
 *
 * Values 0-38: CTCSS tones (0=off, 1-38=67.0-254.1 Hz)
 * Values 39-121: DCS codes (023-754)
 */
enum sa818_tone_code {
  SA818_TONE_NONE = 0, /**< No CTCSS/DCS */

  /* CTCSS Tones (1-38) */
  SA818_CTCSS_67_0 = 1,   /**< 67.0 Hz */
  SA818_CTCSS_71_9 = 2,   /**< 71.9 Hz */
  SA818_CTCSS_74_4 = 3,   /**< 74.4 Hz */
  SA818_CTCSS_77_0 = 4,   /**< 77.0 Hz */
  SA818_CTCSS_79_7 = 5,   /**< 79.7 Hz */
  SA818_CTCSS_82_5 = 6,   /**< 82.5 Hz */
  SA818_CTCSS_85_4 = 7,   /**< 85.4 Hz */
  SA818_CTCSS_88_5 = 8,   /**< 88.5 Hz */
  SA818_CTCSS_91_5 = 9,   /**< 91.5 Hz */
  SA818_CTCSS_94_8 = 10,  /**< 94.8 Hz */
  SA818_CTCSS_97_4 = 11,  /**< 97.4 Hz */
  SA818_CTCSS_100_0 = 12, /**< 100.0 Hz */
  SA818_CTCSS_103_5 = 13, /**< 103.5 Hz */
  SA818_CTCSS_107_2 = 14, /**< 107.2 Hz */
  SA818_CTCSS_110_9 = 15, /**< 110.9 Hz */
  SA818_CTCSS_114_8 = 16, /**< 114.8 Hz */
  SA818_CTCSS_118_8 = 17, /**< 118.8 Hz */
  SA818_CTCSS_123_0 = 18, /**< 123.0 Hz */
  SA818_CTCSS_127_3 = 19, /**< 127.3 Hz */
  SA818_CTCSS_131_8 = 20, /**< 131.8 Hz */
  SA818_CTCSS_136_5 = 21, /**< 136.5 Hz */
  SA818_CTCSS_141_3 = 22, /**< 141.3 Hz */
  SA818_CTCSS_146_2 = 23, /**< 146.2 Hz */
  SA818_CTCSS_151_4 = 24, /**< 151.4 Hz */
  SA818_CTCSS_156_7 = 25, /**< 156.7 Hz */
  SA818_CTCSS_162_2 = 26, /**< 162.2 Hz */
  SA818_CTCSS_167_9 = 27, /**< 167.9 Hz */
  SA818_CTCSS_173_8 = 28, /**< 173.8 Hz */
  SA818_CTCSS_179_9 = 29, /**< 179.9 Hz */
  SA818_CTCSS_186_2 = 30, /**< 186.2 Hz */
  SA818_CTCSS_192_8 = 31, /**< 192.8 Hz */
  SA818_CTCSS_203_5 = 32, /**< 203.5 Hz */
  SA818_CTCSS_210_7 = 33, /**< 210.7 Hz */
  SA818_CTCSS_218_1 = 34, /**< 218.1 Hz */
  SA818_CTCSS_225_7 = 35, /**< 225.7 Hz */
  SA818_CTCSS_233_6 = 36, /**< 233.6 Hz */
  SA818_CTCSS_241_8 = 37, /**< 241.8 Hz */
  SA818_CTCSS_250_3 = 38, /**< 250.3 Hz */

  /* DCS Codes (39-121) - Standard DCS codes from 023 to 754 */
  SA818_DCS_023 = 39,  /**< DCS 023N */
  SA818_DCS_025 = 40,  /**< DCS 025N */
  SA818_DCS_026 = 41,  /**< DCS 026N */
  SA818_DCS_031 = 42,  /**< DCS 031N */
  SA818_DCS_032 = 43,  /**< DCS 032N */
  SA818_DCS_036 = 44,  /**< DCS 036N */
  SA818_DCS_043 = 45,  /**< DCS 043N */
  SA818_DCS_047 = 46,  /**< DCS 047N */
  SA818_DCS_051 = 47,  /**< DCS 051N */
  SA818_DCS_053 = 48,  /**< DCS 053N */
  SA818_DCS_054 = 49,  /**< DCS 054N */
  SA818_DCS_065 = 50,  /**< DCS 065N */
  SA818_DCS_071 = 51,  /**< DCS 071N */
  SA818_DCS_072 = 52,  /**< DCS 072N */
  SA818_DCS_073 = 53,  /**< DCS 073N */
  SA818_DCS_074 = 54,  /**< DCS 074N */
  SA818_DCS_114 = 55,  /**< DCS 114N */
  SA818_DCS_115 = 56,  /**< DCS 115N */
  SA818_DCS_116 = 57,  /**< DCS 116N */
  SA818_DCS_122 = 58,  /**< DCS 122N */
  SA818_DCS_125 = 59,  /**< DCS 125N */
  SA818_DCS_131 = 60,  /**< DCS 131N */
  SA818_DCS_132 = 61,  /**< DCS 132N */
  SA818_DCS_134 = 62,  /**< DCS 134N */
  SA818_DCS_143 = 63,  /**< DCS 143N */
  SA818_DCS_145 = 64,  /**< DCS 145N */
  SA818_DCS_152 = 65,  /**< DCS 152N */
  SA818_DCS_155 = 66,  /**< DCS 155N */
  SA818_DCS_156 = 67,  /**< DCS 156N */
  SA818_DCS_162 = 68,  /**< DCS 162N */
  SA818_DCS_165 = 69,  /**< DCS 165N */
  SA818_DCS_172 = 70,  /**< DCS 172N */
  SA818_DCS_174 = 71,  /**< DCS 174N */
  SA818_DCS_205 = 72,  /**< DCS 205N */
  SA818_DCS_212 = 73,  /**< DCS 212N */
  SA818_DCS_223 = 74,  /**< DCS 223N */
  SA818_DCS_225 = 75,  /**< DCS 225N */
  SA818_DCS_226 = 76,  /**< DCS 226N */
  SA818_DCS_243 = 77,  /**< DCS 243N */
  SA818_DCS_244 = 78,  /**< DCS 244N */
  SA818_DCS_245 = 79,  /**< DCS 245N */
  SA818_DCS_246 = 80,  /**< DCS 246N */
  SA818_DCS_251 = 81,  /**< DCS 251N */
  SA818_DCS_252 = 82,  /**< DCS 252N */
  SA818_DCS_255 = 83,  /**< DCS 255N */
  SA818_DCS_261 = 84,  /**< DCS 261N */
  SA818_DCS_263 = 85,  /**< DCS 263N */
  SA818_DCS_265 = 86,  /**< DCS 265N */
  SA818_DCS_266 = 87,  /**< DCS 266N */
  SA818_DCS_271 = 88,  /**< DCS 271N */
  SA818_DCS_274 = 89,  /**< DCS 274N */
  SA818_DCS_306 = 90,  /**< DCS 306N */
  SA818_DCS_311 = 91,  /**< DCS 311N */
  SA818_DCS_315 = 92,  /**< DCS 315N */
  SA818_DCS_325 = 93,  /**< DCS 325N */
  SA818_DCS_331 = 94,  /**< DCS 331N */
  SA818_DCS_332 = 95,  /**< DCS 332N */
  SA818_DCS_343 = 96,  /**< DCS 343N */
  SA818_DCS_346 = 97,  /**< DCS 346N */
  SA818_DCS_351 = 98,  /**< DCS 351N */
  SA818_DCS_356 = 99,  /**< DCS 356N */
  SA818_DCS_364 = 100, /**< DCS 364N */
  SA818_DCS_365 = 101, /**< DCS 365N */
  SA818_DCS_371 = 102, /**< DCS 371N */
  SA818_DCS_411 = 103, /**< DCS 411N */
  SA818_DCS_412 = 104, /**< DCS 412N */
  SA818_DCS_413 = 105, /**< DCS 413N */
  SA818_DCS_423 = 106, /**< DCS 423N */
  SA818_DCS_431 = 107, /**< DCS 431N */
  SA818_DCS_432 = 108, /**< DCS 432N */
  SA818_DCS_445 = 109, /**< DCS 445N */
  SA818_DCS_446 = 110, /**< DCS 446N */
  SA818_DCS_452 = 111, /**< DCS 452N */
  SA818_DCS_454 = 112, /**< DCS 454N */
  SA818_DCS_455 = 113, /**< DCS 455N */
  SA818_DCS_462 = 114, /**< DCS 462N */
  SA818_DCS_464 = 115, /**< DCS 464N */
  SA818_DCS_465 = 116, /**< DCS 465N */
  SA818_DCS_466 = 117, /**< DCS 466N */
  SA818_DCS_503 = 118, /**< DCS 503N */
  SA818_DCS_506 = 119, /**< DCS 506N */
  SA818_DCS_516 = 120, /**< DCS 516N */
  SA818_DCS_523 = 121  /**< DCS 523N */
  /* Note: SA818 supports codes up to 121 in this encoding */
};

/**
 * @brief SA818 volume level (1-8)
 */
enum sa818_volume_level {
  SA818_VOLUME_1 = 1, /**< Volume level 1 (quietest) */
  SA818_VOLUME_2 = 2,
  SA818_VOLUME_3 = 3,
  SA818_VOLUME_4 = 4, /**< Volume level 4 (default) */
  SA818_VOLUME_5 = 5,
  SA818_VOLUME_6 = 6,
  SA818_VOLUME_7 = 7,
  SA818_VOLUME_8 = 8 /**< Volume level 8 (loudest) */
};
/**
 * @brief SA818 audio filter flags (can be OR'ed together)
 */
enum sa818_filter_flags : uint8_t {
  SA818_FILTER_NONE = 0,                                                                          /**< No filters enabled */
  SA818_FILTER_PRE_EMPHASIS = (1 << 0),                                                           /**< Pre-emphasis filter (0x01) */
  SA818_FILTER_HIGH_PASS = (1 << 1),                                                              /**< High-pass filter (0x02) */
  SA818_FILTER_LOW_PASS = (1 << 2),                                                               /**< Low-pass filter (0x04) */
  SA818_FILTER_ALL = (SA818_FILTER_PRE_EMPHASIS | SA818_FILTER_HIGH_PASS | SA818_FILTER_LOW_PASS) /**< All filters enabled */
};
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
 * @brief Establish connection handshake with SA818 module
 *
 * Sends AT+DMOCONNECT command to establish communication with the radio module.
 * This should typically be called after power-on to verify UART communication.
 *
 * @param dev SA818 device
 * @return SA818_OK on success, error code on failure
 */
[[nodiscard]] enum sa818_result sa818_at_connect(const struct device *dev);

/**
 * @brief Configure radio group (frequency, CTCSS, squelch)
 *
 * @param dev SA818 device
 * @param bandwidth Channel bandwidth (12.5 or 25 kHz)
 * @param freq_tx TX frequency in MHz (e.g. 145.500)
 * @param freq_rx RX frequency in MHz (e.g. 145.500)
 * @param ctcss_tx TX CTCSS/DCS tone code
 * @param squelch Squelch level (0-8)
 * @param ctcss_rx RX CTCSS/DCS tone code
 * @return SA818_OK on success, error code on failure
 */
[[nodiscard]] enum sa818_result sa818_at_set_group(const struct device *dev, enum sa818_bandwidth bandwidth, float freq_tx, float freq_rx,
                                                   enum sa818_tone_code ctcss_tx, enum sa818_squelch_level squelch, enum sa818_tone_code ctcss_rx);

/**
 * @brief Set volume level
 *
 * @param dev SA818 device
 * @param volume Volume level (1-8)
 * @return SA818_OK on success, error code on failure
 */
[[nodiscard]] enum sa818_result sa818_at_set_volume(const struct device *dev, enum sa818_volume_level volume);

/**
 * @brief Configure audio filters
 *
 * @param dev SA818 device
 * @param filters Filter flags (OR'ed combination of sa818_filter_flags)
 * @return SA818_OK on success, error code on failure
 */
[[nodiscard]] enum sa818_result sa818_at_set_filters(const struct device *dev, enum sa818_filter_flags filters);

/**
 * @brief Read RSSI (signal strength)
 *
 * @param dev SA818 device
 * @param rssi Pointer to store RSSI value
 * @return SA818_OK on success, error code on failure
 */
[[nodiscard]] enum sa818_result sa818_at_read_rssi(const struct device *dev, uint8_t *rssi);

/**
 * @brief Read firmware version
 *
 * @param dev SA818 device
 * @param version Buffer to store version string
 * @param version_len Size of version buffer
 * @return SA818_OK on success, error code on failure
 */
[[nodiscard]] enum sa818_result sa818_at_read_version(const struct device *dev, char *version, size_t version_len);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_SA818_INCLUDE_SA818_SA818_AT_H_ */
