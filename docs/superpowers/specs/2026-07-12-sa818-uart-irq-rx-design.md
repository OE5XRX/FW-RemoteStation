# SA818 Interrupt-Driven UART RX — Cleanup Design

- **Date:** 2026-07-12
- **Component:** `drivers/radio/sa818/` (AT command RX path)
- **Type:** Refactor (behavior-preserving) of the existing interrupt-driven UART receive path
- **Status:** Approved for planning

## Background

The SA818 AT-command RX path was recently converted from busy-poll
(`uart_poll_in()` + 1 ms sleeps) to interrupt-driven receive. That change is a
genuine improvement, and the full `tests/sim_shell` suite (79 cases) passes on
`native_sim/native/64`. However the implementation carries avoidable
complexity and one hardware-robustness risk:

> **Note on committed history.** That first IRQ conversion existed only as
> **uncommitted working-tree state** — a byte-wise `k_msgq` implementation
> (`at_rx_msgq`, `at_irq_enabled`) with silent drop-oldest overflow. It was
> never committed on its own; the cleanup below superseded it in the same
> commit. The `git` baseline this work is diffed against (`312d2b8`) is
> therefore still **poll-based**, so the resulting commit reads as
> poll → `ring_buf`/IRQ even though the design was developed against the
> intermediate `k_msgq` version described in points 2–4 below.

1. **ISR can spin on real hardware.** The ISR loops
   `while (true) { uart_irq_update; if (!pending) break; if (!rx_ready) continue; ... }`.
   On `native_sim` `is_pending == rx_ready`, so it is harmless there. On the
   STM32U575, `uart_irq_is_pending()` can stay asserted for a source we do not
   handle (e.g. an error/overrun flag) while `uart_irq_rx_ready()` is false —
   the `continue` then spins forever in interrupt context → lockup.
2. **Triple timeout logic.** `uart_read_response()` checks the timeout in three
   places (top-of-loop elapsed, `remaining_ms <= 0`, and `-EAGAIN` after
   `k_msgq_get`). This is residue from cramming the old poll scaffolding and the
   new blocking read into one function.
3. **Dead fallback path.** A poll fallback (`at_irq_enabled == false`) exists
   only for the case that `uart_irq_callback_user_data_set()` fails at init —
   which in practice never happens. It doubles the branching in the ISR setup,
   the flush, and the read hot-path.
4. **Byte-at-a-time `k_msgq` with silent drop.** RX uses a `k_msgq` of
   `msg_size = 1`, i.e. one waitq/lock cycle per received byte, and on overflow
   it silently drops the oldest byte — corrupting a line-based response with no
   error signal.

## Goals

- Make the ISR robust against a stuck (non-RX) pending interrupt on real HW.
- Collapse the RX read path to a single, clear loop with one timeout check.
- Remove the dead poll fallback (IRQ is mandatory).
- Replace the byte-wise `k_msgq` with the idiomatic Zephyr byte-stream
  primitive (`struct ring_buf` + `k_sem`) and surface overrun honestly.
- **Preserve observable behavior** so the existing `tests/sim_shell` suite stays
  green unchanged — that suite is the regression guard for this refactor.

## Non-Goals

- No change to the TX path (`uart_write_command`) — it stays poll-out.
- No new public API, no change to `enum sa818_result` values.
- No capability/persistence/platform logic (firmware stays thin).
- No band/DT changes.

## Design Decisions (agreed)

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Poll fallback | **Removed — IRQ only** | Init-callback registration never fails in practice on STM32 or native_sim; keeping it doubles branching for a path that never runs. Init failure becomes a hard error. |
| RX data structure | **`ring_buf` + `k_sem`** | Idiomatic Zephyr byte-stream transport; ~no per-byte waitq/lock cost; enables an honest overrun flag. |
| Ring buffer size | **256 bytes** | > `SA818_AT_RESPONSE_MAX_LEN` (128) with burst headroom. |
| Overrun policy | **Log only, no new error code** | Keeps observable behavior identical so tests stay green; overrun cannot realistically occur for AT responses. |

## Detailed Design

### 1. `sa818_priv.h` — runtime data

Remove `k_msgq at_rx_msgq`, `char at_rx_msgq_buffer[...]`, and `bool at_irq_enabled`.
Add:

```c
#define SA818_AT_RX_RB_SIZE 256   /* > SA818_AT_RESPONSE_MAX_LEN (128), burst headroom */

struct sa818_data {
  ...
  struct k_mutex  lock;
  struct ring_buf at_rx_rb;                 /* ISR -> reader byte stream */
  uint8_t         at_rx_rb_buf[SA818_AT_RX_RB_SIZE];
  struct k_sem    at_rx_sem;                /* "data available", binary (max count 1) */
  bool            at_rx_overrun;            /* ISR sets on ring-buffer overflow */
  ...
};
```

`SA818_AT_RX_QUEUE_LEN` is removed.

### 2. ISR — spin-safe (`sa818_at.cpp`)

```c
static void sa818_uart_isr(const struct device *uart, void *user_data) {
  struct sa818_data *data = static_cast<struct sa818_data *>(user_data);

  while (uart_irq_update(uart) && uart_irq_is_pending(uart)) {
    if (!uart_irq_rx_ready(uart)) {
      break;   /* only RX IRQ is enabled; anything else -> leave, do not spin */
    }
    uint8_t buf[16];
    int n = uart_fifo_read(uart, buf, sizeof(buf));
    if (n <= 0) {
      break;
    }
    if (ring_buf_put(&data->at_rx_rb, buf, n) < (uint32_t)n) {
      data->at_rx_overrun = true;
    }
    k_sem_give(&data->at_rx_sem);
  }
}
```

- `break` on `!rx_ready` (instead of `continue`) removes the infinite-loop risk:
  a latched non-RX pending source can no longer trap the ISR. Since only the RX
  IRQ is enabled, in normal operation `pending` implies `rx_ready`, so the break
  is not hit spuriously.
- The semaphore is initialized with a max count of 1, so it saturates and cannot
  grow unbounded. The lost-wakeup case is covered by "give after put; reader
  fully drains the ring buffer after every take".

### 3. `sa818_at_uart_init` — hard init failure

```c
int sa818_at_uart_init(const struct device *dev) {
  const struct sa818_config *cfg = static_cast<const struct sa818_config *>(dev->config);
  struct sa818_data *data = static_cast<struct sa818_data *>(dev->data);

  int ret = uart_irq_callback_user_data_set(cfg->uart, sa818_uart_isr, data);
  if (ret != 0) {
    LOG_ERR("SA818 UART IRQ callback registration failed: %d", ret);
    return ret;
  }
  uart_irq_rx_enable(cfg->uart);
  return 0;
}
```

### 4. `uart_read_response` — single clear loop

Signature drops the `uart` parameter (the reader only ever reads from the ring
buffer):

```c
static sa818_result uart_read_response(struct sa818_data *data, char *response, size_t response_len, uint32_t timeout_ms) {
  if (!data || !response || response_len == 0) {
    return SA818_ERROR_INVALID_PARAM;
  }

  memset(response, 0, response_len);
  size_t pos = 0;
  const int64_t start = k_uptime_get();

  while (pos < response_len - 1) {
    uint8_t c;
    while (ring_buf_get(&data->at_rx_rb, &c, 1) == 1) {   /* drain what is present */
      if (c == '\n') {
        response[pos] = '\0';
        return SA818_OK;
      }
      if (c == '\r') {
        continue;
      }
      response[pos++] = c;
      if (pos >= response_len - 1) {
        break;
      }
    }

    int32_t remaining = static_cast<int32_t>(timeout_ms) - static_cast<int32_t>(k_uptime_get() - start);
    if (remaining <= 0) {
      LOG_ERR("UART read timeout");
      return SA818_ERROR_TIMEOUT;
    }
    if (k_sem_take(&data->at_rx_sem, K_MSEC(remaining)) != 0) {
      LOG_ERR("UART read timeout");
      return SA818_ERROR_TIMEOUT;
    }
  }

  response[response_len - 1] = '\0';
  return SA818_OK;   /* buffer full without newline — same behavior as before */
}
```

### 5. Flush + overrun (`sa818_at_send_command`)

```c
static void uart_flush_rx(struct sa818_data *data) {
  ring_buf_reset(&data->at_rx_rb);
  k_sem_reset(&data->at_rx_sem);
  data->at_rx_overrun = false;
}
```

- Called under `data->lock`, immediately before `uart_write_command()`, as today.
- The `uart_read_response()` call site loses its `uart` argument.
- After a successful read, if `data->at_rx_overrun` is set, emit `LOG_WRN` (the
  response may have been truncated). No error is returned to the caller.

### 6. Init (`sa818_core.cpp`)

Replace the `k_msgq_init(...)` line with:

```c
ring_buf_init(&data->at_rx_rb, sizeof(data->at_rx_rb_buf), data->at_rx_rb_buf);
k_sem_init(&data->at_rx_sem, 0, 1);
data->at_rx_overrun = false;
```

Treat `sa818_at_uart_init(dev)` as a **hard** init error: propagate a non-zero
return instead of the current `LOG_WRN` + continue.

## Error Handling Summary

| Condition | Behavior |
|-----------|----------|
| IRQ callback registration fails at init | Hard init failure (LOG_ERR + non-zero return) |
| Response terminated by `\n` | `SA818_OK`, response NUL-terminated at `\n` |
| Response fills buffer without `\n` | `SA818_OK`, response truncated at `response_len-1` (unchanged) |
| No data within `timeout_ms` | `SA818_ERROR_TIMEOUT` |
| Ring-buffer overflow during a transaction | `at_rx_overrun = true` → `LOG_WRN` after read; no error code |
| NULL/zero params | `SA818_ERROR_INVALID_PARAM` |

## Testing & Verification

- **Regression:** `west twister -T tests/sim_shell -p native_sim/native/64` must
  stay 79/79 green with no test changes — this is the primary guard.
- **Build:** `west build -b native_sim/native/64 app` and (if toolchain
  available) `-b fm_board app`.
- **Format:** `clang-format-18` clean over the touched files.
- **New coverage (if driveable without HW):** evaluate a `tests/sim_shell` case
  that drives a response longer than the ring buffer via the PTY simulator to
  exercise the overrun-logging branch. Add only if it can be driven on
  native_sim; do not gate the refactor on hardware-only tests.

## Risks

- The STM32 `uart_irq_update`/`is_pending` semantics differ from native pty; the
  `break`-on-`!rx_ready` structure is chosen specifically to be safe under both.
  Verified against the canonical Zephyr ISR idiom.
- Ring-buffer sizing: 256 B is comfortably above the 128 B max response; no
  realistic overrun, and overrun is now observable via the flag if assumptions
  change.
