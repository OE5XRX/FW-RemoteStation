# Design: Generic `module describe` + Typed Command Mapping (SA818)

**Date:** 2026-07-01
**Status:** Design (approved, pre-implementation)
**Repo:** `FW-RemoteStation`
**Deliverable:** D1 of the module-platform initiative (FW-RemoteStation #36)
**Parent contract:** `station-manager` meta-spec
`docs/superpowers/specs/2026-06-21-module-platform-sim-bridge-design.md` — this design
implements §5.1 (capability schema), §5.2 (thin firmware) and §8 (Firmware↔Agent contract).

---

## 1. Goal & Scope

Add a thin, generic, machine-readable **module interface** to the firmware that maps onto the
existing SA818 driver. This is the firmware half of the Firmware↔Agent contract (meta-spec §8).
Everything runs against `native_sim`; no hardware.

### In scope
1. Machine-readable `module describe`: identity (`type`/`model`/`version`) + a typed capability list
   (meta-spec §5.1). **Not** parsing the human `fm> ` prompt (meta-spec §9).
2. A generic, typed command mapping: validate `{capability, op, value}` and dispatch to the SA818
   driver + GPIO.
3. Capabilities for SA818-V (2 m): `frequency`, `ptt`, `power_level`, `rssi`, `volume`, `bandwidth`.
4. The generic interface lives **alongside** the existing `sa818` shell tree, which stays intact.

### Out of scope (explicit)
- No broker/server/UI (meta-spec D3/D4/D5). No audio (D6+). No persistence, no platform knowledge,
  no access model in firmware. No generic firmware shell framework (YAGNI). The firmware stays
  **thin & honest** (meta-spec §5.2).

### Leading principle (meta-spec §3)
The firmware is the ground truth of what is physically present and describes itself. The `module`
interface is generic in shape; the SA818-specific knowledge (capability list, driver mapping) is the
single module-specific piece and stays in the SA818 driver directory.

---

## 2. Prerequisite: SA818 driver must actually build

On `main`, `drivers/radio/sa818/Kconfig` declares `depends on UART`, but Zephyr has **no bare
`CONFIG_UART` symbol** (the serial subsystem is `CONFIG_SERIAL`). The dependency therefore evaluates
to `n` and **`CONFIG_SA818` can never be enabled** — the driver never compiles, in the app or in
`tests/sim_shell`. This was verified empirically: with the driver's stock Kconfig, the built
`.config` contains no `CONFIG_SA818`, and 22/40 `sim_shell` pytest cases fail with
`sa818: command not found`.

**Fix (one line):** `depends on UART` → `depends on SERIAL`. This is the identical change already
present on the U575 port branch (PR #37); it is a trivially reconcilable overlap and is unavoidable
for D1 (nothing can be layered on a driver that does not compile). With this fix plus
`CONFIG_SA818_SHELL=y`, the 22 pre-existing sa818 tests pass (39/40 overall; the one remainder is an
unrelated audio test — see §7).

---

## 3. Architecture

A small **object model** split into a generic framework and an SA818-specific layer,
compiled when `CONFIG_SA818_MODULE_IFACE=y`:

**`include/oe5xrx/module_iface.h` — generic framework (no device / RTOS deps, reusable across modules):**
- `Capability` — abstract base owning a typed `FieldSpec` (name/type/unit/constraints/access)
  plus `onSet`/`onGet`/`onDo` hooks. `describe()` renders the descriptor JSON **generically from
  the `FieldSpec`** (one renderer, no per-capability strings), and `handle(op)` enforces the
  op⇄kind contract before delegating (e.g. `set` on telemetry → `read_only`, wrong op → `wrong_op`).
- Kind mixins `Setting` / `Action` / `Telemetry` fix the capability kind and which hooks apply.
- `JsonWriter` — bounded, truncation-safe JSON builder with string escaping (used by both
  `describe` and result rendering, so malformed input can never break the contract).
- `Result` — a success-with-typed-value | error-code outcome that renders `MODULE-RESULT` JSON.
- `Module` — identity + a fixed registry of `Capability*`; renders `describe` and dispatches
  `execute`. Fully device-agnostic and unit-testable on the host.

**`drivers/radio/sa818/sa818_module.cpp` — SA818 concrete layer:**
- One subclass per capability (`FrequencyCap`, `BandwidthCap`, `PowerLevelCap`, `VolumeCap`
  `: Setting`; `PttCap : Action`; `RssiCap : Telemetry`), each encapsulating its own `FieldSpec`,
  value parsing/validation, and SA818 driver mapping.
- A shared `Sa818Context` holds the device handle plus the **RAM group shadow** (bandwidth,
  tx/rx frequency, tones, squelch), seeded to the module's power-on defaults, so
  `set frequency` / `set bandwidth` can rebuild the full `sa818_at_set_group(...)` call (the
  driver has no "frequency-only" entry point). Working state, **not** capability persistence.
- Statically-allocated registry (`Module` + one instance per capability) and the `module`
  Zephyr shell group (`describe`/`set`/`get`/`do`) forwarding to `Module::execute`.

**Extensibility:** adding a capability = one new `Capability` subclass + one registry entry;
a whole new module type reuses `module_iface.h` unchanged with its own capability classes and
`Module` instance. This is the OOP realisation of the meta-spec's "generic shape, device
specifics as low as possible".

```
module describe / set / get / do   (Zephyr shell, one module = implicit "module")
        │
        ▼
Module (registry)  ──►  Capability::handle(op)  ── op⇄kind gating ──►  onSet/onGet/onDo
        │                        │                                          │
        │ describe()             └─ Result / describe rendered via JsonWriter (MODULE-* JSON)
        ▼                                                                    ▼
generic framework (module_iface.h)                         SA818 driver API (sa818.h / sa818_at.h)
                                                            + GPIO   ← UNCHANGED
```

New Kconfig symbol (in `drivers/radio/sa818/Kconfig`, under `if SA818`):

```
config SA818_MODULE_IFACE
  bool "SA818 generic module interface (describe + typed commands)"
  default n
  depends on SHELL
  help
    Registers the generic, machine-readable `module` shell command group
    (describe / set / get / do) mapping onto the SA818 driver. This is the
    Firmware<->Agent contract surface; the human `sa818` command tree stays separate.
```

`CMakeLists.txt` gains `zephyr_library_sources_ifdef(CONFIG_SA818_MODULE_IFACE sa818_module.cpp)`.

---

## 4. Wire-format contract (meta-spec §8)

### 4.1 Input — plain positional shell args (never JSON-in)

Zephyr's shell splits on whitespace; JSON with braces/quotes as an argument is fragile. The module
is implicit (this firmware = one module), so the command carries `{op, capability, value}`:

| Command | op | Meaning |
|---|---|---|
| `module describe` | — | Emit the descriptor |
| `module set <cap> <value>` | `set` | Write a `setting` capability |
| `module get <cap>` | `get` | Read a `telemetry` or current value |
| `module do <cap> <value>` | `do` | Invoke an `action` capability |

`op` maps to `kind`: `setting`→`set`, `telemetry`→`get`, `action`→`do`. The agent knows each
capability's kind from `describe` and picks the matching op.

### 4.2 Output — one token-prefixed compact JSON line (JSON-out)

Each response is a single line: a stable token, a space, then compact JSON. The agent strips ANSI,
finds the line beginning with the token, and `json.loads` the remainder. Single-line keeps the
contract to one read + one parse per response.

**Descriptor** (`module describe`):

```
MODULE-DESCRIBE {"schema":1,"identity":{"type":"fm_transceiver","model":"SA818-V","version":"2m"},"capabilities":[{"name":"frequency","kind":"setting","type":"float","unit":"MHz","min":144.0,"max":148.0,"access":"operator"},{"name":"ptt","kind":"action","type":"bool","access":"operator"},{"name":"power_level","kind":"setting","type":"enum","values":["low","high"],"access":"operator"},{"name":"rssi","kind":"telemetry","type":"int","unit":"dBm","readonly":true,"access":"operator"},{"name":"volume","kind":"setting","type":"int","min":1,"max":8,"access":"operator"},{"name":"bandwidth","kind":"setting","type":"enum","values":["12.5","25"],"unit":"kHz","access":"operator"}]}
```

**Command result** (`set`/`get`/`do`):

```
MODULE-RESULT {"ok":true,"cap":"frequency","op":"set","value":145.5}
MODULE-RESULT {"ok":true,"cap":"rssi","op":"get","value":120}
MODULE-RESULT {"ok":true,"cap":"ptt","op":"do","value":true}
MODULE-RESULT {"ok":false,"cap":"frequency","op":"set","error":"out_of_range"}
```

Error codes (string enum): `unknown_capability`, `bad_value`, `out_of_range`, `read_only`,
`wrong_op`, `driver_error`, `usage`, `too_long`. `too_long` is emitted only on the truncation
fallback path when a pathologically long capability/value would otherwise overflow the output
buffer; the fallback still emits valid JSON in the stable shape (`cap` present, possibly empty).

Notes:
- JSON is produced by the framework's `mod::JsonWriter` (bounded, truncation-safe, with string
  escaping), not hand-built strings. The C locale is dot-decimal, so floats round-trip correctly;
  result floats (`frequency`) are emitted with fixed `%.4f` precision (e.g. `145.5000`, `146.0000`)
  and always render as a JSON float. A pytest `json.loads` on the output guarantees validity.
- `rssi` follows the meta-spec §5.1 example (`int`, unit `dBm`, readonly); the value is the driver's
  reading. Precise dBm calibration is a driver concern, out of scope for D1.

---

## 5. Capability → driver mapping

| Capability | kind | type / constraints | Dispatch |
|---|---|---|---|
| `frequency` | setting | float, MHz, 144.0–148.0 (continuous — no enforced step) | shadow.tx=shadow.rx=value → `sa818_at_set_group(...)` |
| `ptt` | action | bool (`on`/`off`/`1`/`0`/`true`/`false`) | `sa818_set_ptt(SA818_PTT_ON/OFF)` |
| `power_level` | setting | enum `low`/`high` | `sa818_set_power_level(SA818_POWER_LOW/HIGH)` |
| `rssi` | telemetry | int, dBm, readonly | `sa818_at_read_rssi(&v)` |
| `volume` | setting | int, 1–8 | `sa818_at_set_volume(...)` |
| `bandwidth` | setting | enum `12.5`/`25`, kHz | shadow.bw=value → `sa818_at_set_group(...)` |

Device power (PD pin) is intentionally **not** a capability (matches the meta-spec §5.1 SA818-V
list). Validation runs before dispatch: unknown capability → `unknown_capability`; wrong op for the
capability's kind (e.g. `set rssi`) → `read_only`/`wrong_op`; out-of-range/unparseable value →
`out_of_range`/`bad_value`; driver non-`SA818_OK` → `driver_error`.

---

## 6. Data-flow proof (meta-spec §6 / §8 / §9)

The single real datapoint that makes the schema "generic":

1. `module set frequency 145.500` on the shell.
2. `sa818_module.cpp` validates float ∈ [144.0, 148.0].
3. Updates shadow tx/rx and calls `sa818_at_set_group(dev, bw, 145.5, 145.5, tone, sql, tone)`.
4. Driver emits `AT+DMOSETGROUP=...` over UART.
5. `SA818Simulator` parses it → `state.freq_tx == 145.5`, `state.freq_rx == 145.5`.
6. Firmware replies `MODULE-RESULT {"ok":true,"cap":"frequency","op":"set","value":145.5}`.

The pytest asserts both the JSON result **and** `sa818_sim.get_state().freq_tx == 145.5` — end to end.

---

## 7. Testing

**Primary — pytest on `native_sim`** (`tests/sim_shell/pytest/test_module_iface.py`):
- `describe` is valid JSON with identity + the 6 capabilities and their expected fields/constraints.
- **`frequency` E2E**: `module set frequency 145.500` → `ok:true` + `sa818_sim.state.freq_tx == 145.5`.
- out-of-range frequency (`150.0`) → `ok:false`/`out_of_range`, sim state unchanged.
- `ptt` (`do ptt on` → sa818 status `ptt=1`), `power_level` (→ `high_power`), `volume`
  (→ `sim.state.volume`), `bandwidth` (→ `sim.state.bandwidth`), `get rssi` (→ int value).
- error paths: unknown capability, `set` on read-only `rssi`, malformed usage.

**Secondary — ztest (optional, if factoring is clean):** a `native_sim` ztest exercising the pure
helpers (value parsing, range/enum validation, JSON descriptor build) in isolation. Only added if the
helpers factor out cleanly without coupling to the shell; otherwise the pytest E2E is authoritative.

**Enablement:** `tests/sim_shell/prj.conf` gains `CONFIG_SA818_SHELL=y` and
`CONFIG_SA818_MODULE_IFACE=y`. Enabling `SA818_SHELL` also un-breaks the 22 pre-existing sa818 tests.

**Pre-existing unrelated failure:** `test_sim_shell.py::test_wav_load_start_adc` fails deterministically
because a long pytest `tmp_path` line-wraps in the shell echo, so the harness cannot match the echoed
command. This is a test-harness robustness issue in an audio test (out of D1 scope). To let the CI job
go green, the test is pointed at a short path — no change to audio logic.

---

## 8. CI

`.github/workflows/ci.yml`: uncomment the `Twister system tests (shell)` step so
`west twister -T tests/sim_shell -p native_sim/native/64` runs and gates PRs — making the D1 E2E test
part of CI. `clang-format` already covers `app`/`boards`/`tests`; the new `drivers/...` source is
formatted to the same `.clang-format` regardless. `build_and_tests` (app build + `-T app` twister)
is unaffected.

---

## 9. Interface contract summary (Definition of Done, meta-spec §8)

- `module describe` returns identity + capabilities for SA818-V, machine-readable (single
  `MODULE-DESCRIBE` JSON line).
- Typed `frequency` set flows end-to-end and drives the real SA818 driver on `native_sim`, verified
  by `SA818Simulator.state.freq_tx == 145.5`.
- Tests green on `native_sim` (pytest, optional ztest); existing sa818 tests do not break.
- CI green: `build_and_tests` + `clang_format`, plus the newly enabled `sim_shell` job.
- Firmware stays thin (§5.2): no platform knowledge, no capability persistence, no access model.

---

## 10. Risks & mitigations (meta-spec §9)

| Risk | Mitigation |
|---|---|
| Schema over-engineering | Validate strictly against SA818 end-to-end (`frequency`); other modules only cited as schema examples. |
| Brittle shell parsing (human prompt vs machine) | Machine-readable token-prefixed JSON output; positional args in, JSON out; never parse the `fm> ` prompt. |
| Kconfig prereq collides with PR #37 | Identical one-line change; trivially reconcilable at merge. Unavoidable for D1. |
| Audio assumptions creep in | Audio strictly out of scope; only the harness-robustness path fix touches an audio test. |
| Float locale round-trip | Hand-built JSON uses C-locale dot-decimal; pytest `json.loads` guards validity. |
