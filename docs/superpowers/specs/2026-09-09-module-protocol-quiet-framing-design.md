# Module Control-Protocol: Quiet, Pull-Based Status + Atomic Framing

**Status:** Design locked (agreed with maintainer) — pending implementation
**Date:** 2026-09-09
**Target:** `fm_board` (STM32U575) FM module + `native_sim` (dev/CI). USB CDC-ACM control
interface ("OE5XRX FM Transceiver Board", VID:PID 2fe3:0012, model SA818-V).
**Branch:** `feat/module-protocol-quiet-framing`

## 1. Purpose & Scope

The FM module exposes a machine-readable `MODULE-*` control protocol over its CDC-ACM
control UART (`module list` → `MODULE-LIST {…}`, `module <id> describe` →
`MODULE-DESCRIBE {…}`, `module <id> set|get|do …` → `MODULE-RESULT {…}`). The
station-agent uses `list` + `describe` as a discovery handshake.

On real hardware two **unsolicited** streams share that UART and corrupt the handshake:

- **(a)** a **periodic status line** — `SA818 Status - Power: ON, PTT: OFF, SQL: CLOSED`,
  emitted every 10 s by the app main loop as a `LOG_INF` line.
- **(b)** ordinary **Zephyr log lines** (`[HH:MM:SS.mmm,uuu] …`).

Because these are emitted by a *different thread* than the shell, they can arrive **in the
middle of** a `MODULE-*` response line, so the agent parses corrupted JSON and the FM
module flaps **OFFLINE** intermittently in station-manager.

This change makes the control protocol **quiet** (no unsolicited status spam) and
**robustly framed** (protocol responses emitted atomically so logs cannot rip them apart),
and adds an **`audio` capability** to `describe` so the agent can derive audio-path presence
from the capability schema (single source of truth).

### In scope (this repo only — `FW-RemoteStation`)

1. Remove the periodic/unsolicited status line; replace with a **pull** query
   (`module <id> status`) that returns a live state snapshot only on request.
2. Add an **`audio`** capability to the SA818 module's `describe` output.
3. Make `MODULE-*` responses emit **atomically** so interleaved logs cannot corrupt them.

### Out of scope (owned by the parallel station-manager session — do not touch)

- Agent-side discovery robustness / retry, removal of any interim audio flag, interim audio
  handling. `FW-RemoteStation` only defines the firmware half of the contract.
- A second physical interface for logs. **Zephyr logs deliberately stay on the same UART.**
- Persisting capability state, access/role models, platform config — the firmware stays thin.

## 2. Guiding Decisions (locked)

| Decision | Choice | Rationale |
|---|---|---|
| Status delivery | **Pull only** via `module <id> status` | No unsolicited output on the control UART; the agent asks when it wants state |
| Status frame | New line token **`MODULE-STATUS {…}`** | Distinct, unambiguous frame the agent parses like the other `MODULE-*` lines; keeps the `MODULE-RESULT` envelope (per-cap) unchanged |
| Status content | **Generic snapshot of every capability** (`get` value per cap) | Single source of truth — the same caps `describe` advertises are the ones `status` reports; no separate hand-maintained field list to drift |
| Logs on UART | **Kept**, but routed **only through the shell log backend** | Same physical UART, but the shell serializes log output against `shell_print`, so a `MODULE-*` line is never split mid-string |
| Framing mechanism | **Remove the direct `CONFIG_LOG_BACKEND_UART`** backend | It is a *second*, async (`LOG_PROCESS_THREAD`) writer to the same UART with no mutual exclusion vs. the shell — the actual line-ripper. The shell log backend (`CONFIG_SHELL_LOG_BACKEND`, already active) keeps logs flowing, serialized |
| `audio` capability | New `kind:"audio"`, `type:"stream"` descriptor-only cap | The agent treats any `kind=="audio"` cap as "audio path present"; declarative, no coupling of the audio datapath into the module layer |

## 3. Root-Cause Analysis

- **Periodic line:** `app/src/main_usb_audio.cpp` main loop does
  `k_sleep(K_SECONDS(10)); LOG_INF("SA818 Status - Power: …")` forever. This is the
  "NN Status - Power…" line. It is pure unsolicited noise on the control UART.
- **Ripping:** `app/prj.conf` enables **both** `CONFIG_LOG_BACKEND_UART=y` **and** (by the
  shell default) `CONFIG_SHELL_LOG_BACKEND=y`. `CONFIG_LOG_MODE_DEFERRED=y`, so the direct
  UART backend is flushed by the async log-processing thread, concurrently with the shell
  thread's `shell_print`. Two unsynchronized writers on one UART → mid-line corruption.
  Removing the direct UART backend leaves logs flowing through the shell backend, which
  runs log flushing and command output on the *same* shell thread — so a `shell_print`
  response line is emitted whole.

## 4. Protocol Contract Changes

### 4.1 New verb: `module <id> status`

```
module fm status
→ MODULE-STATUS {"schema":1,"module":"fm","values":{
     "frequency":145.5000,"tx_frequency":145.5000,"rx_frequency":145.5000,
     "ptt":false,"power_level":"low","rssi":0,"volume":4,"bandwidth":"12.5",
     "squelch":4,"tx_tone":"none","rx_tone":"none","band":"vhf","audio":"uac2"}}
```

- Rendered by iterating the module's capability registry and calling each cap's `get`.
- Each cap's value is rendered with the **same** value serialization as `MODULE-RESULT`
  (`int`→integer, `float`→`%.4f`, `bool`→`true/false`, `string`→quoted, unavailable→`null`).
- A cap whose `get` returns an error renders as `null` (snapshot never fails as a whole
  for one bad cap).
- Truncation-safe: if the snapshot overflows its buffer, emit
  `MODULE-STATUS {"schema":1,"module":"fm","error":"too_long"}` (mirrors the `describe`
  fallback) rather than truncated JSON.
- Unknown module id → `MODULE-RESULT {…,"op":"status","error":"unknown_module"}` (reuses the
  standard error envelope, matching how `describe` reports an unknown module).

### 4.2 New capability: `audio`

Added to `module fm describe` capability list:

```json
{"name":"audio","kind":"audio","type":"stream","access":"operator"}
```

- Advertised **unconditionally** (it is part of the SA818 FM-module design contract; the
  agent keys off its presence, not off a runtime probe).
- `module fm get audio` → `MODULE-RESULT {…,"cap":"audio","op":"get","value":"uac2"}`
  (declarative transport identifier; side-effect-free).
- `set` / `do` on `audio` → `wrong_op` (it is not a setting or an action).

### 4.3 Unchanged

`MODULE-LIST`, `MODULE-DESCRIBE`, `MODULE-RESULT` frames and all existing `set/get/do`
semantics are unchanged. The human `sa818 …` command tree is untouched.

## 5. Framework Extension (`include/oe5xrx/module/iface.h`)

The generic capability framework gains, with no device/RTOS dependencies:

- `enum class Kind` gains `Audio` (`kindStr` → `"audio"`).
- `enum class ValueType` gains `Stream` (`typeStr` → `"stream"`). It represents a
  non-scalar stream endpoint; it has no numeric range/enum rendering.
- A get-only mixin `AudioInfo : public Capability` — `get`→`onGet`, `set`/`do`→`wrong_op`.
- `Result::renderValueOnly(JsonWriter&)` — public; renders just the value arm (or `null`
  when the result is an error), for the aggregate `status` snapshot.
- `Module::snapshot(JsonWriter&) const` — renders
  `{"schema":1,"module":<id>,"values":{<cap>:<value>,…}}` by calling each cap's
  `handle(Op::Get, "")`.

## 6. Testing

`native_sim` = hardware (project rule). All protocol behavior is system-tested via the
`tests/sim_shell` pytest harness (`SA818Simulator` over a PTY):

- `describe` now includes `audio` with the exact shape above.
- `module fm status` returns one `MODULE-STATUS` line whose `values` object contains every
  advertised capability, with correct types (float as number, ptt as bool, etc.).
- `status` reflects live state (e.g. after `set frequency 146.0`, `values.frequency == 146.0`;
  after `do ptt on`, `values.ptt == true`).
- Existing `MODULE-RESULT`/`set/get/do` tests continue to pass unchanged.

The **framing** fix (config) is verified structurally: the shipped `fm_board` build's
resolved `.config` has `CONFIG_LOG_BACKEND_UART` **unset** and `CONFIG_SHELL_LOG_BACKEND=y`;
the periodic-status string is absent from the binary. The mid-line-rip symptom itself is a
hardware/timing artifact confirmed on real HW at flash time (see §7) — it is not
reproducible on `native_sim` (huge host stack, different UART wiring), so it is not gated by
a native_sim test.

`tests/sim_shell/prj.conf` is **left unchanged** (native_sim log routing is irrelevant to
the hardware framing bug; changing it risks destabilizing the green harness).

## 7. Hardware Validation (post-merge, by maintainer)

Firmware flash to the FM module is a separate DFU/SWD step, not remote via the CM4. After
merge + build, the maintainer flashes and verifies on the real board:

1. Idle CDC-ACM control UART shows **no** periodic `Status - Power…` line (only
   solicited responses + genuine log events).
2. `module fm status` returns a single well-formed `MODULE-STATUS` line.
3. `module fm describe` includes the `audio` capability.
4. Under concurrent logging (e.g. during an SA818 handshake), repeated `module fm describe`
   / `list` responses are never split mid-line — the station-agent discovery no longer flaps.
```
build/zephyr/zephyr.bin           # bare/debug via SWD, or
build/app/zephyr/zephyr.signed.bin # prod via: dfu-util --alt <slot1> --download …
```
