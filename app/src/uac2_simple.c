#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/class/usbd_uac2.h>

LOG_MODULE_REGISTER(uac2_simple, LOG_LEVEL_INF);

/* Passe das an deinen DTS Label an (usb_audio2 / uac2_radio etc.) */
#define UAC2_NODE DT_NODELABEL(uac2_radio)

static const struct device *uac2_dev;

/* simple static buffers */
#define USB_BUF_SIZE 256
static uint8_t out_buf[USB_BUF_SIZE];
// static uint8_t in_buf[USB_BUF_SIZE];

/* ---- Callbacks ------------------------------------------------ */

static void sof_cb(const struct device *dev, void *ctx) {
  ARG_UNUSED(dev);
  ARG_UNUSED(ctx);
  /* wird jede USB Frame (1ms bei FS) aufgerufen */
}

static void terminal_update_cb(const struct device *dev, uint8_t terminal, bool enabled, bool suspended, void *ctx) {
  LOG_INF("terminal %u: enabled=%d suspended=%d", terminal, enabled, suspended);
}

/* Host will OUT Daten schicken → wir liefern Buffer */
static void *get_recv_buf(const struct device *dev, uint8_t terminal, uint16_t size, void *ctx) {
  ARG_UNUSED(dev);
  ARG_UNUSED(ctx);

  if (size > USB_BUF_SIZE) {
    LOG_ERR("requested %u bytes, buffer too small", size);
    return NULL;
  }

  return out_buf;
}

/* OUT Daten angekommen */
static void data_recv_cb(const struct device *dev, uint8_t terminal, void *buf, uint16_t size, void *ctx) {
  ARG_UNUSED(dev);
  ARG_UNUSED(ctx);

  LOG_INF("RX terminal %u: %u bytes", terminal, size);

  /* hier würdest du Richtung DAC / Radio weiterleiten */
}

/* Buffer freigeben (bei statischen Buffern leer lassen) */
static void buf_release_cb(const struct device *dev, uint8_t terminal, void *buf, void *ctx) {
  ARG_UNUSED(dev);
  ARG_UNUSED(terminal);
  ARG_UNUSED(buf);
  ARG_UNUSED(ctx);
}

/* Optional: Feedback (für async playback)
 * 8kHz mono => 8 samples / frame
 * Full-Speed uses Q10.14 format
 */
static uint32_t feedback_cb(const struct device *dev, uint8_t terminal, void *ctx) {
  ARG_UNUSED(dev);
  ARG_UNUSED(ctx);

  /* nur für OUT-Terminal */
  if (terminal != 0) {
    return 0;
  }

  return (8u << 14); /* 8 samples/frame */
}

/* ---- Ops Tabelle --------------------------------------------- */

static const struct uac2_ops uac2_ops_table = {
    .sof_cb = sof_cb,
    .terminal_update_cb = terminal_update_cb,
    .get_recv_buf = get_recv_buf,
    .data_recv_cb = data_recv_cb,
    .buf_release_cb = buf_release_cb,
    .feedback_cb = feedback_cb,
};

/* ---- Init ---------------------------------------------------- */

static int uac2_simple_init(void) {
  uac2_dev = DEVICE_DT_GET(UAC2_NODE);
  if (!device_is_ready(uac2_dev)) {
    LOG_ERR("UAC2 device not ready");
    return -ENODEV;
  }

  usbd_uac2_set_ops(uac2_dev, &uac2_ops_table, NULL);

  LOG_INF("UAC2 simple initialized");
  return 0;
}

/* nach USB Init starten */
SYS_INIT(uac2_simple_init, APPLICATION, 80);
