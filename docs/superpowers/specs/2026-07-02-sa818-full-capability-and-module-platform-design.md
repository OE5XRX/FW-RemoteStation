# Design: SA818 Full Capability + Module-Platform Restructure

**Date:** 2026-07-02
**Status:** Implemented on PR #38 (this is the authoritative shipped contract)
**Repo:** `FW-RemoteStation`
**Builds on:** D1 (`docs/superpowers/specs/2026-07-01-module-describe-and-typed-command-mapping-design.md`)
**Delivered on:** PR #38 (extends the D1 branch `feature/d1-module-describe`)
**Parent contract:** module-platform meta-spec (`station-manager` `2026-06-21-module-platform-sim-bridge-design.md`) §4 (`{module, capability, op, value}`), §5.1, §5.2, §8.

---

## 1. Goal & context

D1 delivered the generic `module` interface proven end-to-end with a single `frequency`
capability. This deliverable makes the SA818 module **production-real** and pushes the
architecture to be **generic and dynamic from the start**, so it scales to future device
classes (HF radios with several discrete bands, LTE, …) without special-casing. Five threads:

1. **Restructure** — extract the module-platform out of the SA818 *driver* into its own Zephyr
   subsystem. The driver is hardware; the capability layer is implementation on top of it.
2. **Multi-module** — the shell/registry addresses modules by id (`module <id> …` + `module list`).
3. **Variant** — support SA818-**U** (70 cm) as well as SA818-**V** (2 m), selected from devicetree.
4. **Generic constraints** — a numeric capability advertises a **list of named ranges** (a named
   range = a band), not a single `min`/`max`. Scales from one range (SA818) to many (HF).
5. **Full radio config** — expose repeater split (`tx_frequency`/`rx_frequency`), `squelch`,
   `tx_tone`/`rx_tone`, plus a readonly `band` telemetry; report `rssi` as raw.

**Guardrails (repo standard, `CLAUDE.md`/`CONTRIBUTING.md`):** modern C++17/20, OOP where it
carries a real interface; **no dynamic allocation**; **no exceptions/RTTI** (status/return types);
Zephyr-C idiom at the driver boundary; firmware stays thin (no persistence/access/platform logic);
`build_and_tests` + `clang_format` green.

---

## 2. Restructure: `subsys/module/`

Three layers, cleanly separated:

| Layer | Location | Nature |
|---|---|---|
| Hardware driver | `drivers/radio/sa818/` (core/at/audio/shell) | Zephyr C driver — **unchanged except the tone-API addition in §7** |
| Generic framework | `include/oe5xrx/module/iface.h` (header-only, namespace `mod`) | device-agnostic capability model |
| SA818 module layer | `subsys/module/devices/sa818/sa818_module.cpp` | binds the framework onto the SA818 driver; registers the `module` shell |

- **Move:** `drivers/radio/sa818/sa818_module.cpp` → `subsys/module/devices/sa818/sa818_module.cpp`;
  `include/oe5xrx/module_iface.h` → `include/oe5xrx/module/iface.h` (include guard stays
  `OE5XRX_MODULE_IFACE_H_`; consumers use `#include <oe5xrx/module/iface.h>`).
- **New build wiring:**
  - `subsys/module/Kconfig` + `subsys/module/CMakeLists.txt`.
  - Root `Kconfig` gains `rsource "subsys/module/Kconfig"`; root `CMakeLists.txt` gains
    `add_subdirectory(subsys/module)`.
  - `subsys/module/CMakeLists.txt`: `zephyr_library_sources_ifdef(CONFIG_MODULE_SA818 devices/sa818/sa818_module.cpp)`.
- **Kconfig rename:** `CONFIG_SA818_MODULE_IFACE` → split into:
  ```kconfig
  menuconfig MODULE
    bool "OE5XRX module platform (self-describing device capability interface)"
  if MODULE
  config MODULE_SA818
    bool "SA818 FM transceiver module"
    depends on SHELL
    depends on SA818
    select CBPRINTF_FP_SUPPORT
    help
      Registers the generic `module` shell interface for the SA818 FM transceiver.
  endif # MODULE
  ```
  The old `SA818_MODULE_IFACE` symbol in the driver's Kconfig is removed. `sa818_module.cpp`
  guards on `#ifdef CONFIG_MODULE_SA818`.
- **Test config:** `tests/sim_shell/prj.conf` swaps `CONFIG_SA818_MODULE_IFACE=y` for
  `CONFIG_MODULE=y` + `CONFIG_MODULE_SA818=y` (which `select`s `SA818` and `CBPRINTF_FP_SUPPORT`).
  `CONFIG_SA818_SHELL=y` stays (existing human-shell tests).

Restructure is a **pure move** — behavior and wire format identical; the full `sim_shell` suite
stays green with the D1 grammar before any feature work begins.

---

## 3. Generic framework changes (`iface.h`)

### 3.1 Numeric constraints → list of named ranges
Replace the single-range `FieldSpec` fields (`hasRange`/`min`/`max`) with a list:

```cpp
struct Range {
  const char *name; // optional band label (nullptr for a plain scalar range)
  double min;
  double max;
};

struct FieldSpec {
  const char *name;
  ValueType type;
  const char *unit = nullptr;
  const Range *ranges = nullptr;   // for int/float
  size_t rangeCount = 0;
  const char *const *enumValues = nullptr;  // for enum
  size_t enumCount = 0;
  bool readonly = false;
  const char *access = "operator";
};
```

- `describe()` renders numeric constraints as
  `"ranges":[{"name":"vhf","min":134.0,"max":174.0}, …]` (the `"name"` key omitted when null).
- A helper `bool FieldSpec::inAnyRange(double v) const` (value ∈ some range) is provided so
  capabilities validate against the advertised ranges — descriptor and validation share one source.
- Plain scalars use one unnamed range: `volume` → `[{nullptr,1,8}]`, `squelch` → `[{nullptr,0,8}]`.

### 3.2 Module identity + registry (multi-module)
- `Module` gains an `id` (`const char *`), exposed at the top of `describe`:
  `MODULE-DESCRIBE {"schema":1,"module":"fm","identity":{…},"capabilities":[…]}`.
- New `ModuleRegistry` (framework): a `std::span<Module *const>` with `find(id)` and `list(JsonWriter&)`.
- `MODULE-RESULT` gains a `"module"` field:
  `MODULE-RESULT {"ok":true,"module":"fm","cap":"frequency","op":"set","value":145.5000}`.
- New error code `unknown_module` (for an id not in the registry).

### 3.3 Shell grammar (SA818 layer)
Single `module` command, arg-routed (an id is not a static subcommand):

| Command | Output |
|---|---|
| `module list` | `MODULE-LIST {"modules":["fm"]}` |
| `module <id> describe` | `MODULE-DESCRIBE {…}` |
| `module <id> set <cap> <value>` | `MODULE-RESULT {…}` |
| `module <id> get <cap>` | `MODULE-RESULT {…}` |
| `module <id> do <cap> <value>` | `MODULE-RESULT {…}` |

Unknown id → `MODULE-RESULT {"ok":false,"module":"<id>","op":"…","error":"unknown_module"}`.
Missing args → `usage`. This matches the meta-spec's `{module, capability, op, value}` model (§4).

---

## 4. SA818-U / SA818-V variant (devicetree)

The two parts differ only in frequency range; interface and AT protocol are identical.

- **Binding** `dts/bindings/radio/sa,sa818.yaml`: add
  ```yaml
  band:
    type: string
    required: true
    enum: ["vhf", "uhf"]
    description: RF band of the fitted SA818 part (SA818-V = vhf, SA818-U = uhf).
  ```
- **native_sim overlay** sets `band = "vhf"` on the `sa818` node.
- The SA818 layer reads `DT_ENUM_IDX(DT_NODELABEL(sa818), band)` at **compile time** and selects:
  - identity `model` = `"SA818-V"` / `"SA818-U"`, `version` = `"vhf"` / `"uhf"`;
  - the frequency `Range` = `{"vhf",134.0,174.0}` / `{"uhf",400.0,480.0}` — **full hardware range**
    (chosen deliberately; no amateur-band limiting in firmware).
- Firmware still hard-validates the SA818's absolute limits inside `sa818_at_set_group`
  (134–174 / 400–480) as a backstop.

---

## 5. Capability set (SA818 module `fm`)

Shadow group state (working state, not persistence):
`{sa818_bandwidth bw, float freq_tx, float freq_rx, sa818_tone_code tone_tx, sa818_tone_code tone_rx,
sa818_squelch_level squelch}`, seeded to defaults (12.5 kHz, 145.500/145.500, none/none, level 4).
Every setting that changes any of these rebuilds `sa818_at_set_group(bw, freq_tx, freq_rx, tone_tx,
squelch, tone_rx)` and commits the shadow only on `SA818_OK`.

| Capability | kind | type / constraints | Behavior |
|---|---|---|---|
| `frequency` | setting | float, band range | simplex: set → `freq_tx = freq_rx = v`; get → `freq_rx` |
| `tx_frequency` | setting | float, band range | set → `freq_tx = v`; get → `freq_tx` (repeater TX) |
| `rx_frequency` | setting | float, band range | set → `freq_rx = v`; get → `freq_rx` (repeater RX) |
| `ptt` | action | bool | `sa818_set_ptt`; get → status ptt |
| `power_level` | setting | enum `low`/`high` | `sa818_set_power_level`; get → status |
| `volume` | setting | int, range 1–8 | `sa818_at_set_volume`; get → status volume |
| `bandwidth` | setting | enum `12.5`/`25`, kHz | shadow bw; get → shadow bw |
| `squelch` | setting | int, range 0–8 | shadow squelch; get → shadow squelch |
| `tx_tone` | setting | string (`none`/CTCSS Hz/DCS) | parse → shadow tone_tx; get → formatted |
| `rx_tone` | setting | string (`none`/CTCSS Hz/DCS) | parse → shadow tone_rx; get → formatted |
| `rssi` | telemetry | int, unit **`raw`**, readonly | `sa818_at_read_rssi` (raw 0–255; no dBm calibration) |
| `band` | telemetry | string, readonly | name of the range that `freq_rx` currently falls in, else `null` |

Notes:
- `frequency`/`tx_frequency`/`rx_frequency` share the same band `Range` (from §4).
- `band` is generic: it reports the active named range for any module (here always `vhf`/`uhf`;
  for a future HF module it returns e.g. `20m`). Returns JSON `null` when off-grid.
- Tone strings map via the driver API in §7. `none` ⇢ `SA818_TONE_NONE`.

---

## 6. Data-flow example (repeater + tone)

`module fm set rx_frequency 145.600` then `module fm set tx_frequency 145.000` then
`module fm set tx_tone 67.0`:
1. `rx_frequency` → shadow `freq_rx=145.6` → `AT+DMOSETGROUP=0,145.0000,145.6000,0000,4,0000`.
2. `tx_frequency` → shadow `freq_tx=145.0` → group rebuilt.
3. `tx_tone 67.0` → `sa818_at_parse_tone("67.0")` = CTCSS code 1 → group
   `AT+DMOSETGROUP=0,145.0000,145.6000,0001,4,0000`.
4. `SA818Simulator` observes `freq_tx==145.0`, `freq_rx==145.6`, `ctcss_tx==1`.
Each `MODULE-RESULT` echoes `{module:"fm", cap, op, value}`.

---

## 7. Driver enhancement: tone parse/format API

The CTCSS/DCS ↔ string mapping is currently a private `parse_tone` in `sa818_shell.cpp`. Promote it
to the driver AT API so it is the single source of truth for both the human shell and the module:

- `sa818_tone_code sa818_at_parse_tone(const char *s);`
  — `"none"`/`"off"` → `SA818_TONE_NONE`; CTCSS Hz (`"67.0"`…`"250.3"`) → 1–38; DCS (`"023"`…) → 39–121;
  invalid → `SA818_TONE_NONE`. (Logic lifted from the shell's `parse_tone`, extended to accept
  3-digit DCS code strings so `tx_tone`/`rx_tone` can set DCS — the shell path is unaffected.)
- `int sa818_at_tone_to_str(sa818_tone_code code, char *buf, size_t len);`
  — reverse: `0` → `"none"`, 1–38 → CTCSS Hz string, 39–121 → DCS code string (e.g. `"023"`).
  Returns bytes written or negative on truncation.
- Refactor `sa818_shell.cpp` to call `sa818_at_parse_tone` (delete its static copy). No behavior change.

Both functions live in `sa818_at.cpp` / declared in `drivers/radio/sa818/sa818/sa818_at.h`, backed
by a single static CTCSS-Hz table and a DCS-code table.

---

## 8. Testing (`tests/sim_shell/pytest`)

The `SA818Simulator` already tracks `bandwidth`, `freq_tx`, `freq_rx`, `ctcss_tx`, `ctcss_rx`,
`squelch`, `volume`, `rssi`, so E2E assertions are direct.

- **Migration:** every existing `test_module_iface.py` case moves to the id-addressed grammar
  (`module fm describe`, `module fm set …`), and the `_payload` helper tolerates the new `module` field.
- **New coverage:**
  - `module list` → `{"modules":["fm"]}`.
  - describe: top-level `module:"fm"`; identity `model:"SA818-V"`, `version:"vhf"`; `rssi` unit `raw`;
    `frequency`/`tx_frequency`/`rx_frequency` carry `ranges:[{"name":"vhf","min":134.0,"max":174.0}]`.
  - repeater split: `set tx_frequency`/`set rx_frequency` → distinct `sim.freq_tx`/`freq_rx`; `frequency`
    still sets both equal.
  - `squelch` → `sim.squelch`.
  - `tx_tone`/`rx_tone`: `set tx_tone 67.0` → `sim.ctcss_tx==1`; `set rx_tone 023` → DCS code;
    `get` round-trips to `"67.0"`/`"023"`; `set … none` → 0.
  - `get band` → `"vhf"`.
  - `module xyz describe` → `unknown_module`.
  - existing error paths (`bad_value`/`out_of_range`/`read_only`/`wrong_op`/`unknown_capability`/
    `too_long`) preserved.
- **Driver:** unit-level assertions that `sa818_at_parse_tone`/`sa818_at_tone_to_str` round-trip
  (via a shell exercise or the module tone tests). Existing `sa818` shell tests stay green.
- CI: `build_and_tests` (app + `fm_board` + `sim_shell`) and `clang_format` green.

---

## 9. Wire-contract summary (updated Firmware↔Agent contract, §8)

- `module list` → `MODULE-LIST {"modules":[…]}`.
- `module <id> describe` → `MODULE-DESCRIBE {"schema":1,"module":"<id>","identity":{type,model,version},
  "capabilities":[…]}`; numeric capabilities carry `"ranges":[{name?,min,max}]`.
- `module <id> <set|get|do> …` → `MODULE-RESULT {"ok":…,"module":"<id>","cap":…,"op":…,
  "value"|"error":…}`.
- Error codes: `unknown_module`, `unknown_capability`, `bad_value`, `out_of_range`, `read_only`,
  `wrong_op`, `driver_error`, `usage`, `too_long`.
- All strings JSON-escaped; floats fixed `%.4f`; non-finite floats → `null`; truncation → valid
  short fallback. `rssi` is raw (unit `raw`), not dBm.

---

## 10. Risks & mitigations

| Risk | Mitigation |
|---|---|
| Contract change breaks D1 consumers | D1's agent/server not built yet; the grammar change *aligns* with meta-spec §4. Migrate all tests in the same PR. |
| Ranges model over-generalizes | One uniform `ranges` list; validation via one helper. Scalars are a single unnamed range — no special cases. |
| Restructure churn | Task 1 is a pure move with the suite green before any feature work. |
| Tone reverse-map (code→string) drift | Single bidirectional table in the driver; both directions unit-tested via round-trip. |
| Variant selection wrong at build | `band` is required in the binding; `DT_ENUM_IDX` fails the build if absent/typo'd. |
| SA818 has no dBm calibration | Report `rssi` raw (unit `raw`); any dBm mapping lives in the agent/server. |

---

## 11. Implementation phases (for the plan)

1. **Restructure** — move to `subsys/module/`, header to `include/oe5xrx/module/`, Kconfig rename,
   test-config swap. Pure move, suite green (D1 grammar).
2. **Ranges model** — `Range`/`ranges` in `FieldSpec` + `describe` + `inAnyRange`; SA818 single range;
   `rssi` unit → `raw`. Tests updated for `ranges`.
3. **Multi-module** — `Module` id + `ModuleRegistry` + id-addressed shell + `module` field +
   `unknown_module` + `module list`; migrate tests to `module fm`.
4. **Variant** — binding `band` prop + overlay + compile-time identity/range selection. Tests.
5. **Repeater split** — `tx_frequency`/`rx_frequency`; shadow → `freq_tx`/`freq_rx`. Tests.
6. **Squelch** — `squelch` capability. Tests.
7. **Tones** — driver `sa818_at_parse_tone`/`sa818_at_tone_to_str` + shell refactor; `tx_tone`/
   `rx_tone` capabilities. Tests.
8. **Band telemetry** — `band` readonly capability. Tests.
9. Review (atlas/audit/probe) + Copilot loop.

Each phase keeps `sim_shell` green and is committed independently on PR #38's branch.
