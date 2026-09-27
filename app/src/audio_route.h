/**
 * @file audio_route.h
 * @brief Pure routing decision for the UAC2 audio tap (USB OUT / SA818 RX).
 *
 * Single source of truth for *where* an audio chunk goes in the USB audio
 * bridge, factored out of usb_audio_bridge.cpp so the decision is unit-testable
 * on native_sim without the USB device stack (which the bridge translation unit
 * pulls in and native_sim does not provide). The functions are pure, constexpr,
 * and have no RTOS/USB/heap dependencies.
 *
 * Production behaviour (CONFIG_FM_TEST_LOOPBACK=n): the bridge always passes
 * loopback_enabled == false, so classify_out() folds to OutRoute::TxRing and
 * classify_sa818_rx() folds to Sa818RxRoute::RxRing -- the normal
 * USB OUT -> SA818 TX and SA818 RX -> USB IN paths, unchanged.
 *
 * @copyright Copyright (c) 2026 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#ifndef AUDIO_ROUTE_H_
#define AUDIO_ROUTE_H_

namespace audio_route {

/** Destination for a just-received USB OUT (host -> device) PCM chunk. */
enum class OutRoute {
  /** Normal operation: OUT -> TX ring -> SA818 TX. */
  TxRing,
  /** Test loopback armed and USB IN active: OUT -> RX ring -> USB IN (SA818 bypassed). */
  RxRingLoopback,
  /** Test loopback armed but USB IN inactive: drop. Buffering here would fill the
   *  RX ring with stale audio that plays out delayed (or overflows) once IN comes up. */
  DropInInactive,
};

/** Destination for an SA818 RX capture chunk. */
enum class Sa818RxRoute {
  /** Normal operation: SA818 RX -> RX ring -> USB IN. */
  RxRing,
  /** Test loopback armed: drop, so the real capture cannot mix into the RX ring
   *  which is carrying the looped-back USB OUT audio. */
  DropLoopback,
};

/**
 * @brief Decide where a USB OUT PCM chunk is routed.
 * @param loopback_enabled test loopback armed at runtime
 * @param rx_enabled USB IN (device -> host) terminal currently active
 */
constexpr OutRoute classify_out(bool loopback_enabled, bool rx_enabled) {
  if (!loopback_enabled) {
    return OutRoute::TxRing;
  }
  return rx_enabled ? OutRoute::RxRingLoopback : OutRoute::DropInInactive;
}

/**
 * @brief Decide where an SA818 RX capture chunk is routed.
 * @param loopback_enabled test loopback armed at runtime
 */
constexpr Sa818RxRoute classify_sa818_rx(bool loopback_enabled) {
  return loopback_enabled ? Sa818RxRoute::DropLoopback : Sa818RxRoute::RxRing;
}

} // namespace audio_route

#endif /* AUDIO_ROUTE_H_ */
