# SA818 Full Capability + Module-Platform Restructure — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the D1 `module` interface into a full, generic module platform: extract it into its own Zephyr subsystem, make it multi-module (id-addressed), add SA818-U/-V devicetree variants, a generic named-ranges (band) constraint model, and the full SA818 radio config (repeater split, squelch, tones, band telemetry, raw rssi).

**Architecture:** Three layers — the SA818 hardware driver (`drivers/radio/sa818/`, unchanged except a tone-API addition), a device-agnostic header-only framework (`include/oe5xrx/module/iface.h`, namespace `mod`), and the SA818 capability layer in a new subsystem (`subsys/module/devices/sa818/sa818_module.cpp`). The framework holds `Capability`/`Setting`/`Action`/`Telemetry`, `Result`, `JsonWriter`, `Module`, and a new `ModuleRegistry`; the SA818 layer registers one `Module` (`fm`) whose capabilities map onto the driver.

**Tech Stack:** Zephyr RTOS (C++20, no heap/exceptions/RTTI), `native_sim/native/64`, pytest + `twister_harness` + the PTY-based `SA818Simulator`.

**Spec:** `docs/superpowers/specs/2026-07-02-sa818-full-capability-and-module-platform-design.md`
**Builds on D1 (same PR #38 branch `feature/d1-module-describe`).**

## Global Constraints

- **Repo C++ standard (`CLAUDE.md`/`CONTRIBUTING.md`):** modern C++17/20; OOP where it carries a real interface; **no dynamic allocation** (no `new`/`delete`/`malloc`/`std::vector`/`std::string`/`std::function`); **no exceptions, no RTTI**; endorsed static types (`std::array`, `std::span`, `std::string_view`, `std::optional`, `constexpr`, fixed buffers). Dereference `std::optional` with `*`/`value_or` after checking, never `.value()`.
- **Zephyr-C idiom at the driver boundary** — the SA818 driver API stays C; C++ lives in the module layer.
- **Firmware thin** — no persistence/access/platform logic. The RAM group-shadow is working state only.
- **Wire tokens are contract:** `MODULE-LIST `, `MODULE-DESCRIBE `, `MODULE-RESULT ` each followed by one line of compact JSON. Error codes: `unknown_module`, `unknown_capability`, `bad_value`, `out_of_range`, `read_only`, `wrong_op`, `driver_error`, `usage`, `too_long`. Floats fixed `%.4f`; non-finite → `null`; all strings JSON-escaped; truncation → valid short fallback.
- **Identity:** `type=fm_transceiver`; per band: `model=SA818-V`/`SA818-U`, `version=vhf`/`uhf`. Module id `fm`.
- **One PR (#38), one branch.** Commit + push after every task. `clang_format` (binary `clang-format-18`) must pass over `app`/`boards`/`tests`; format new `drivers`/`subsys`/`include` files too.
- **sim_shell stays green (100%) after every task.**

### Build / test environment (already provisioned)

Every build/test command runs with this exact prefix:

```bash
cd /home/pbuchegger/OE5XRX && . /home/pbuchegger/OE5XRX/.zephyr-venv/bin/activate && \
  export ZEPHYR_BASE=/home/pbuchegger/OE5XRX/zephyr ZEPHYR_TOOLCHAIN_VARIANT=host && \
  cd FW-RemoteStation && <west ...>
```

- Full suite (REQUIRED `-c` clobber after any Kconfig/CMake/source change): `west twister -T tests/sim_shell -p native_sim/native/64 -c --inline-logs`
- App parity: `west build -b native_sim/native/64 app -p always`

---

## File Structure

- **Move** `include/oe5xrx/module_iface.h` → `include/oe5xrx/module/iface.h` (framework; guard stays `OE5XRX_MODULE_IFACE_H_`).
- **Move** `drivers/radio/sa818/sa818_module.cpp` → `subsys/module/devices/sa818/sa818_module.cpp` (SA818 capability layer).
- **Create** `subsys/module/Kconfig`, `subsys/module/CMakeLists.txt`.
- **Modify** root `CMakeLists.txt`, root `Kconfig`, `drivers/radio/sa818/CMakeLists.txt`, `drivers/radio/sa818/Kconfig`, `tests/sim_shell/prj.conf`.
- **Modify** `drivers/radio/sa818/sa818/sa818_at.h` + `drivers/radio/sa818/sa818_at.cpp` + `drivers/radio/sa818/sa818_shell.cpp` (tone parse/format API, Task 7).
- **Modify** `dts/bindings/radio/sa,sa818.yaml` + `app/boards/native_sim_native_64.overlay` (band property, Task 4).
- **Modify** `tests/sim_shell/pytest/test_module_iface.py` (migrate + extend).

---

## Task 1: Restructure into `subsys/module/` (pure move)

**Deliverable:** the module layer compiles from the new subsystem; behavior and wire format identical; suite green with the D1 grammar (`module describe`).

**Files:** git-move 2 files; create 2; modify 5.

- [ ] **Step 1: Move the two files (preserve history)**

```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation
mkdir -p subsys/module/devices/sa818 include/oe5xrx/module
git mv include/oe5xrx/module_iface.h include/oe5xrx/module/iface.h
git mv drivers/radio/sa818/sa818_module.cpp subsys/module/devices/sa818/sa818_module.cpp
```

- [ ] **Step 2: Create `subsys/module/Kconfig`**

```kconfig
# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

menuconfig MODULE
  bool "OE5XRX module platform (self-describing device capability interface)"
  help
    Generic, machine-readable module capability framework (the `module` shell
    interface). Concrete device modules are enabled below.

if MODULE

config MODULE_SA818
  bool "SA818 FM transceiver module"
  default n
  depends on SHELL
  select SA818
  select CBPRINTF_FP_SUPPORT
  help
    Registers the generic `module` shell interface for the SA818 FM transceiver,
    mapping the capability contract onto the SA818 driver.

endif # MODULE
```

- [ ] **Step 3: Create `subsys/module/CMakeLists.txt`**

```cmake
# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_MODULE_SA818 devices/sa818/sa818_module.cpp)
```

- [ ] **Step 4: Wire it into the root build**

Root `CMakeLists.txt` — final content:
```cmake
# Export the reusable, device-agnostic module framework headers (include/oe5xrx/...).
zephyr_include_directories(include)

add_subdirectory(drivers)
add_subdirectory(subsys/module)
```

Root `Kconfig` — final content:
```kconfig
rsource "drivers/Kconfig"
rsource "subsys/module/Kconfig"
```

- [ ] **Step 5: Remove the module bits from the driver**

In `drivers/radio/sa818/CMakeLists.txt`, delete the block:
```cmake
zephyr_library_sources_ifdef(
  CONFIG_SA818_MODULE_IFACE
  sa818_module.cpp)
```

In `drivers/radio/sa818/Kconfig`, delete the entire `config SA818_MODULE_IFACE … endif`-preceding block (the `SA818_MODULE_IFACE` symbol and its help), leaving `config SA818` and `config SA818_SHELL` intact.

- [ ] **Step 6: Update the moved source**

In `subsys/module/devices/sa818/sa818_module.cpp`:
- change the include `#include <oe5xrx/module_iface.h>` → `#include <oe5xrx/module/iface.h>`
- change both guards `#ifdef CONFIG_SA818_MODULE_IFACE` → `#ifdef CONFIG_MODULE_SA818` and the trailing `#endif /* CONFIG_SA818_MODULE_IFACE */` → `#endif /* CONFIG_MODULE_SA818 */`

- [ ] **Step 7: Update the test config** — `tests/sim_shell/prj.conf`

Replace the module-interface block:
```conf
# Generic module interface under test (describe + typed commands).
# It `select`s CBPRINTF_FP_SUPPORT itself (needed for MODULE-RESULT %.4f floats),
# so no manual float-printf config is required here.
CONFIG_SA818_MODULE_IFACE=y
```
with:
```conf
# Module platform + SA818 device module under test. MODULE_SA818 selects SA818
# and CBPRINTF_FP_SUPPORT (needed for MODULE-RESULT %.4f floats).
CONFIG_MODULE=y
CONFIG_MODULE_SA818=y
```
Leave `CONFIG_SA818=y` and `CONFIG_SA818_SHELL=y` as they are.

- [ ] **Step 8: Build + run — verify identical behavior**

```bash
cd /home/pbuchegger/OE5XRX && . /home/pbuchegger/OE5XRX/.zephyr-venv/bin/activate && \
  export ZEPHYR_BASE=/home/pbuchegger/OE5XRX/zephyr ZEPHYR_TOOLCHAIN_VARIANT=host && \
  cd FW-RemoteStation && west twister -T tests/sim_shell -p native_sim/native/64 -c --inline-logs 2>&1 | \
  grep -E "executed test cases passed|scenario\(s\) failed"
```
Expected: `60 of 60 … passed (100.00%)`, 0 failed.

- [ ] **Step 9: Format + commit**

```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation && clang-format-18 -i subsys/module/devices/sa818/sa818_module.cpp include/oe5xrx/module/iface.h && \
git add -A && git commit -m "refactor(module): extract module platform into subsys/module/

Move the device-agnostic framework header to include/oe5xrx/module/iface.h and the
SA818 capability layer to subsys/module/devices/sa818/. The sa818 driver stays pure
hardware. Rename CONFIG_SA818_MODULE_IFACE -> CONFIG_MODULE + CONFIG_MODULE_SA818.

Pure move, sim_shell 60/60 unchanged. Refs #36." && git push
```

---

## Task 2: Generic named-ranges constraint model + raw rssi

**Deliverable:** numeric capabilities advertise a list of named `ranges`; `rssi` unit is `raw`.

**Files:** Modify `include/oe5xrx/module/iface.h`, `subsys/module/devices/sa818/sa818_module.cpp`, `tests/sim_shell/pytest/test_module_iface.py`.

**Interfaces:**
- Produces: `struct mod::Range { const char *name; double min; double max; }`; `FieldSpec` fields `const Range *ranges; size_t rangeCount;`; `bool FieldSpec::inAnyRange(double) const`.

- [ ] **Step 1: Write/adjust the failing describe test** — in `test_module_iface.py::test_module_describe_valid_json`, replace the frequency/volume constraint assertions and the rssi unit assertion:

```python
    # frequency now advertises named ranges instead of flat min/max
    assert caps["frequency"]["ranges"] == [{"name": "vhf", "min": 134.0, "max": 174.0}]
    assert caps["volume"]["ranges"] == [{"min": 1, "max": 8}]
    assert caps["rssi"]["unit"] == "raw"
```
(Remove any old `caps["frequency"]["min"]/["max"]` and `caps["volume"]["min"]/["max"]` and `unit=="dBm"` assertions.)

- [ ] **Step 2: Run to verify it fails**

```bash
cd /home/pbuchegger/OE5XRX && . /home/pbuchegger/OE5XRX/.zephyr-venv/bin/activate && export ZEPHYR_BASE=/home/pbuchegger/OE5XRX/zephyr ZEPHYR_TOOLCHAIN_VARIANT=host && cd FW-RemoteStation && west twister -T tests/sim_shell -p native_sim/native/64 -c --inline-logs 2>&1 | grep -E "test_module_describe_valid_json|scenario\(s\) failed"
```
Expected: FAIL (`ranges`/`raw` not present).

- [ ] **Step 3: Framework — add `Range`, change `FieldSpec`, render ranges**

In `include/oe5xrx/module/iface.h`, add above `struct FieldSpec`:
```cpp
/** A numeric constraint range; a named range is a band (e.g. "vhf", "20m"). */
struct Range {
  const char *name; // optional band label; nullptr for a plain scalar range
  double min;
  double max;
};
```
Replace the `FieldSpec` range members (`bool hasRange; double min; double max;`) with:
```cpp
  const Range *ranges = nullptr;
  size_t rangeCount = 0;
```
Add to `FieldSpec` a membership test:
```cpp
  bool inAnyRange(double v) const {
    for (size_t i = 0; i < rangeCount; ++i) {
      if (v >= ranges[i].min && v <= ranges[i].max) {
        return true;
      }
    }
    return false;
  }
```
In `Capability::describe`, replace the old `if (s.hasRange) { … min … max … }` block with a ranges renderer (uses the existing private `numStr`):
```cpp
    if (s.ranges != nullptr && s.rangeCount > 0) {
      w.ch(',');
      w.key("ranges");
      w.ch('[');
      char b[24];
      for (size_t i = 0; i < s.rangeCount; ++i) {
        if (i != 0) {
          w.ch(',');
        }
        w.ch('{');
        if (s.ranges[i].name != nullptr) {
          w.kvStr("name", s.ranges[i].name);
          w.ch(',');
        }
        numStr(b, sizeof(b), s.ranges[i].min, s.type);
        w.kvRaw("min", b);
        w.ch(',');
        numStr(b, sizeof(b), s.ranges[i].max, s.type);
        w.kvRaw("max", b);
        w.ch('}');
      }
      w.ch(']');
    }
```

- [ ] **Step 4: SA818 layer — range constants + validation + rssi unit**

In `subsys/module/devices/sa818/sa818_module.cpp`, replace the `FREQ_MIN_MHZ/FREQ_MAX_MHZ/VOLUME_MIN/VOLUME_MAX` constants and the FieldSpec range args with `Range` tables (using `mod::Range`; add `using mod::Range;`):
```cpp
const Range FREQ_RANGES[] = {{"vhf", 134.0, 174.0}};
const Range VOLUME_RANGES[] = {{nullptr, 1.0, 8.0}};

const FieldSpec FREQ_SPEC{"frequency", ValueType::Float, "MHz", FREQ_RANGES, 1};
const FieldSpec VOLUME_SPEC{"volume", ValueType::Int, nullptr, VOLUME_RANGES, 1};
```
Update the other specs to the new field order (no range args for ptt/power/bandwidth; rssi unit `"raw"`):
```cpp
const FieldSpec PTT_SPEC{"ptt", ValueType::Bool};
const FieldSpec POWER_SPEC{"power_level", ValueType::Enum, nullptr, nullptr, 0, POWER_LEVELS, 2};
const FieldSpec RSSI_SPEC{"rssi", ValueType::Int, "raw", nullptr, 0, nullptr, 0, /*readonly=*/true};
const FieldSpec BW_SPEC{"bandwidth", ValueType::Enum, "kHz", nullptr, 0, BANDWIDTHS, 2};
```
In `FrequencyCap::onSet`, replace the range check with:
```cpp
    if (!FREQ_SPEC.inAnyRange(static_cast<double>(*f))) {
      return Result::err("out_of_range");
    }
```
In `VolumeCap::onSet`, replace the `*v < VOLUME_MIN || *v > VOLUME_MAX` check with:
```cpp
    if (!VOLUME_SPEC.inAnyRange(static_cast<double>(*v))) {
      return Result::err("out_of_range");
    }
```

- [ ] **Step 5: Run to verify pass** — twister command from Step 2. Expected: `test_module_describe_valid_json` PASS; 60/60.

- [ ] **Step 6: Format + commit**

```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation && clang-format-18 -i include/oe5xrx/module/iface.h subsys/module/devices/sa818/sa818_module.cpp && \
git add -A && git commit -m "feat(module): generic named-ranges constraint model; rssi raw

FieldSpec numeric constraints become a list of named ranges (a named range = a band),
rendered as \"ranges\":[{name?,min,max}]. Validation via FieldSpec::inAnyRange. rssi unit
dBm -> raw (no official SA818 calibration). Refs #36." && git push
```

---

## Task 3: Multi-module registry + id-addressed shell grammar

**Deliverable:** `module list` + `module <id> describe|set|get|do …`; results carry `"module"`; `unknown_module` for bad ids. All tests migrated to `module fm`.

**Files:** Modify `include/oe5xrx/module/iface.h`, `subsys/module/devices/sa818/sa818_module.cpp`, `tests/sim_shell/pytest/test_module_iface.py`.

**Interfaces:**
- Produces: `Module(const Identity&, const char *id, std::span<Capability *const>)`, `const char *Module::id()`; `Result::render(JsonWriter&, const char *module, const char *cap, const char *op)`; `class ModuleRegistry { Module *find(const char*); void list(JsonWriter&); }`.

- [ ] **Step 1: Migrate + extend the tests** — in `test_module_iface.py`:
  - Update `_payload`/helpers untouched; change every command string `module describe`→`module fm describe`, `module set …`→`module fm set …`, `module get …`→`module fm get …`, `module do …`→`module fm do …` (including the `banana`/error cases: `module fm set banana 1`, etc.).
  - In `test_module_describe_valid_json`, add `assert d["module"] == "fm"`.
  - Add new tests:
```python
def test_module_list(shell):
    out = shell.exec_command("module list")
    for l in _lines(out):
        i = l.find("MODULE-LIST ")
        if i != -1:
            d = json.loads(l[i + len("MODULE-LIST "):])
            assert d == {"modules": ["fm"]}
            return
    raise AssertionError(f"no MODULE-LIST line: {_lines(out)}")


def test_module_unknown_module(shell):
    out = shell.exec_command("module nope describe")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "unknown_module"


def test_module_result_carries_module_id(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module fm set frequency 145.500")
    r = _payload(out, "MODULE-RESULT")
    assert r["module"] == "fm" and r["cap"] == "frequency"
```

- [ ] **Step 2: Run to verify failure** — twister command. Expected: many `module …` tests FAIL (`command`/id parsing) + new tests fail.

- [ ] **Step 3: Framework — Module id, Result.module, ModuleRegistry**

In `iface.h`:
- `Module` constructor + accessor:
```cpp
  Module(const Identity &id, const char *moduleId, std::span<Capability *const> caps)
      : id_(id), moduleId_(moduleId), caps_(caps) {}
  const char *moduleId() const { return moduleId_; }
```
add member `const char *moduleId_;` (next to `Identity id_;`).
- In `Module::describe`, add the module id right after `schema`:
```cpp
    w.ch('{');
    w.kvRaw("schema", "1");
    w.ch(',');
    w.kvStr("module", moduleId_);
    w.ch(',');
    w.key("identity");
```
- `Result::render` gains a module arg:
```cpp
  void render(JsonWriter &w, const char *module, const char *cap, const char *op) const {
    w.ch('{');
    w.kvRaw("ok", ok_ ? "true" : "false");
    w.ch(',');
    w.kvStr("module", module);
    w.ch(',');
    w.kvStr("cap", cap);
    w.ch(',');
    w.kvStr("op", op);
    w.ch(',');
    if (ok_) {
      w.key("value");
      renderValue(w);
    } else {
      w.kvStr("error", err_ != nullptr ? err_ : "error");
    }
    w.ch('}');
  }
```
- `Module::execute` stays returning `Result`. Add a registry after the `Module` class:
```cpp
/** @brief A fixed set of modules addressable by id. */
class ModuleRegistry {
public:
  explicit ModuleRegistry(std::span<Module *const> modules) : modules_(modules) {}

  Module *find(const char *id) const {
    for (Module *m : modules_) {
      if (strcmp(m->moduleId(), id) == 0) {
        return m;
      }
    }
    return nullptr;
  }

  void list(JsonWriter &w) const {
    w.ch('{');
    w.key("modules");
    w.ch('[');
    bool first = true;
    for (Module *m : modules_) {
      if (!first) {
        w.ch(',');
      }
      first = false;
      w.quoted(m->moduleId());
    }
    w.ch(']');
    w.ch('}');
  }

private:
  std::span<Module *const> modules_;
};
```

- [ ] **Step 4: SA818 layer — registry + arg-routed `module` command**

In `sa818_module.cpp`:
- `using mod::ModuleRegistry;`
- registry after the caps registry:
```cpp
Module g_module{g_identity, "fm", g_caps};
Module *const g_modules[] = {&g_module};
ModuleRegistry g_registry{g_modules};
```
(remove the old `Module g_module{g_identity, g_caps};` form).
- Replace the shell command handlers + registration. Delete `cmd_module_describe/set/get/do` and the `SHELL_STATIC_SUBCMD_SET_CREATE(module_cmds, …)`; replace with a single arg-routed handler. `emit_result` gains the module id; add an `emit_describe` helper:
```cpp
void emit_result(const struct shell *sh, const Result &r, const char *module, const char *cap, Op op) {
  char buf[RESULT_BUF_SIZE];
  mod::JsonWriter w(buf, sizeof(buf));
  w.raw("MODULE-RESULT ");
  r.render(w, module, cap, mod::opStr(op));
  if (w.truncated()) {
    shell_print(sh, "MODULE-RESULT {\"ok\":false,\"module\":\"\",\"cap\":\"\",\"op\":\"%s\",\"error\":\"too_long\"}", mod::opStr(op));
    return;
  }
  shell_print(sh, "%s", w.c_str());
}

int cmd_module(const struct shell *sh, size_t argc, char **argv) {
  if (argc >= 2 && !strcmp(argv[1], "list")) {
    char buf[RESULT_BUF_SIZE];
    mod::JsonWriter w(buf, sizeof(buf));
    w.raw("MODULE-LIST ");
    g_registry.list(w);
    shell_print(sh, "%s", w.c_str());
    return 0;
  }
  if (argc < 3) {
    emit_result(sh, Result::err("usage"), argc >= 2 ? argv[1] : "", "", Op::Get);
    return 0;
  }

  const char *id = argv[1];
  const char *op = argv[2];
  Module *m = g_registry.find(id);

  if (!strcmp(op, "describe")) {
    if (m == nullptr) {
      emit_result(sh, Result::err("unknown_module"), id, "", Op::Get);
      return 0;
    }
    char buf[DESCRIBE_BUF_SIZE];
    mod::JsonWriter w(buf, sizeof(buf));
    w.raw("MODULE-DESCRIBE ");
    m->describe(w);
    if (w.truncated()) {
      shell_print(sh, "MODULE-DESCRIBE {\"schema\":1,\"error\":\"too_long\"}");
      return 0;
    }
    shell_print(sh, "%s", w.c_str());
    return 0;
  }

  Op o;
  const char *cap = argv[3 - 1 + 1]; // argv[3]
  const char *value = "";
  if (!strcmp(op, "set")) {
    if (argc < 5) {
      emit_result(sh, Result::err("usage"), id, argc >= 4 ? argv[3] : "", Op::Set);
      return 0;
    }
    o = Op::Set;
    value = argv[4];
  } else if (!strcmp(op, "get")) {
    o = Op::Get;
  } else if (!strcmp(op, "do")) {
    if (argc < 5) {
      emit_result(sh, Result::err("usage"), id, argc >= 4 ? argv[3] : "", Op::Do);
      return 0;
    }
    o = Op::Do;
    value = argv[4];
  } else {
    emit_result(sh, Result::err("usage"), id, "", Op::Get);
    return 0;
  }

  Result r = (m != nullptr) ? m->execute(o, cap, value) : Result::err("unknown_module");
  emit_result(sh, r, id, cap, o);
  return 0;
}
```
(Note: `cap = argv[3]` requires `argc >= 4`; for `get` with `argc < 4` guard first — add at the top of the get branch: `if (argc < 4) { emit_result(sh, Result::err("usage"), id, "", Op::Get); return 0; }`. Write `const char *cap = argv[3];` only after the arg-count guards, i.e. move the `cap` assignment below the per-op guards.)

Register the single command:
```cpp
SHELL_CMD_REGISTER(module, NULL, "module list | module <id> describe|set|get|do <cap> [value]", cmd_module);
```

- [ ] **Step 5: Run to verify pass** — twister command; all migrated + new tests PASS; 60→63/63.

- [ ] **Step 6: Format + commit**

```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation && clang-format-18 -i include/oe5xrx/module/iface.h subsys/module/devices/sa818/sa818_module.cpp && \
git add -A && git commit -m "feat(module): id-addressed multi-module shell + registry

Module gains an id; new ModuleRegistry; shell becomes 'module list' + 'module <id>
describe|set|get|do'. Results carry a \"module\" field; unknown id -> unknown_module.
Aligns with meta-spec {module, capability, op, value}. Refs #36." && git push
```

---

## Task 4: SA818-U / -V variant via devicetree `band`

**Deliverable:** the `band` devicetree property selects identity + frequency range at compile time.

**Files:** Modify `dts/bindings/radio/sa,sa818.yaml`, `app/boards/native_sim_native_64.overlay`, `subsys/module/devices/sa818/sa818_module.cpp`, `tests/sim_shell/pytest/test_module_iface.py`.

- [ ] **Step 1: Write the failing test** — in `test_module_describe_valid_json` add:
```python
    assert d["identity"]["model"] == "SA818-V"
    assert d["identity"]["version"] == "vhf"
```
(and keep the `frequency` ranges assertion from Task 2: `[{"name":"vhf","min":134.0,"max":174.0}]`).

- [ ] **Step 2: Run to verify failure** — twister; expect the model/version assertion to fail (currently model `SA818-V`, version `2m`).

- [ ] **Step 3: Binding — add the `band` property** — in `dts/bindings/radio/sa,sa818.yaml`, under `properties:`:
```yaml
  band:
    type: string
    required: true
    enum:
      - "vhf"
      - "uhf"
    description: >
      RF band of the fitted SA818 part. SA818-V = vhf (134-174 MHz),
      SA818-U = uhf (400-480 MHz).
```

- [ ] **Step 4: Overlay — set the band** — in `app/boards/native_sim_native_64.overlay`, add `band = "vhf";` inside the `sa818` node.

- [ ] **Step 5: SA818 layer — compile-time variant selection** — in `sa818_module.cpp`, above the specs:
```cpp
/* Band selected from devicetree (sa818 node `band` property). */
#define SA818_BAND_IDX DT_ENUM_IDX(DT_NODELABEL(sa818), band) // 0 = vhf, 1 = uhf

#if SA818_BAND_IDX == 1
constexpr const char *BAND_NAME = "uhf";
constexpr const char *BAND_MODEL = "SA818-U";
const Range FREQ_RANGES[] = {{"uhf", 400.0, 480.0}};
#else
constexpr const char *BAND_NAME = "vhf";
constexpr const char *BAND_MODEL = "SA818-V";
const Range FREQ_RANGES[] = {{"vhf", 134.0, 174.0}};
#endif
```
(Delete the Task-2 fixed `FREQ_RANGES` — this replaces it.) Change the identity to use the band:
```cpp
const Identity g_identity{"fm_transceiver", BAND_MODEL, BAND_NAME};
```

- [ ] **Step 6: Run to verify pass** — twister; model `SA818-V`, version `vhf`, frequency range `vhf` 134–174; 63/63.

- [ ] **Step 7: Format + commit**

```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation && clang-format-18 -i subsys/module/devices/sa818/sa818_module.cpp && \
git add -A && git commit -m "feat(module): SA818-U/-V variant via devicetree band property

New required 'band' enum (vhf/uhf) on the sa,sa818 binding + native_sim overlay. The
module selects identity model/version and the frequency range at compile time from
DT_ENUM_IDX. Refs #36." && git push
```

---

## Task 5: Repeater split — `tx_frequency` / `rx_frequency`

**Deliverable:** independent TX/RX frequency capabilities; `frequency` stays simplex; shadow tracks both.

**Files:** Modify `subsys/module/devices/sa818/sa818_module.cpp`, `tests/sim_shell/pytest/test_module_iface.py`.

- [ ] **Step 1: Write the failing tests** — append to `test_module_iface.py`:
```python
def test_module_repeater_split(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    assert _payload(shell.exec_command("module fm set rx_frequency 145.600"), "MODULE-RESULT")["ok"]
    assert _payload(shell.exec_command("module fm set tx_frequency 145.000"), "MODULE-RESULT")["ok"]
    assert sa818_sim.get_state().freq_rx == 145.6
    assert sa818_sim.get_state().freq_tx == 145.0
    # frequency (simplex) sets both equal
    assert _payload(shell.exec_command("module fm set frequency 144.800"), "MODULE-RESULT")["ok"]
    assert sa818_sim.get_state().freq_tx == 144.8
    assert sa818_sim.get_state().freq_rx == 144.8


def test_module_get_tx_rx_frequency(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    shell.exec_command("module fm set tx_frequency 145.000")
    shell.exec_command("module fm set rx_frequency 145.600")
    assert _payload(shell.exec_command("module fm get tx_frequency"), "MODULE-RESULT")["value"] == 145.0
    assert _payload(shell.exec_command("module fm get rx_frequency"), "MODULE-RESULT")["value"] == 145.6
```

- [ ] **Step 2: Run to verify failure** — twister; new tests fail (`unknown_capability`).

- [ ] **Step 3: Shadow → tx/rx + new capabilities** — in `sa818_module.cpp`:
- In `Sa818Context`, replace `float freq;` with `float freq_tx; float freq_rx;` and update the seed `g_ctx{… , SA818_BW_12_5_KHZ, 145.500f, 145.500f, SA818_TONE_NONE, SA818_SQL_LEVEL_4}` (matching the struct field order).
- Update `FrequencyCap::onSet` to set both and `onGet` to return `freq_rx`; rebuild uses both:
```cpp
  Result onSet(const char *value) override {
    if (!ctx_.ready()) return Result::err("driver_error");
    std::optional<float> f = parse_float(value);
    if (!f) return Result::err("bad_value");
    if (!FREQ_SPEC.inAnyRange(static_cast<double>(*f))) return Result::err("out_of_range");
    if (sa818_at_set_group(ctx_.dev, ctx_.bw, *f, *f, ctx_.tone_tx, ctx_.squelch, ctx_.tone_rx) != SA818_OK)
      return Result::err("driver_error");
    ctx_.freq_tx = *f;
    ctx_.freq_rx = *f;
    return Result::okFloat(static_cast<double>(*f));
  }
  Result onGet() override {
    if (!ctx_.ready()) return Result::err("driver_error");
    return Result::okFloat(static_cast<double>(ctx_.freq_rx));
  }
```
- Add `TXFREQ_SPEC`/`RXFREQ_SPEC` (share `FREQ_RANGES`):
```cpp
const FieldSpec TXFREQ_SPEC{"tx_frequency", ValueType::Float, "MHz", FREQ_RANGES, 1};
const FieldSpec RXFREQ_SPEC{"rx_frequency", ValueType::Float, "MHz", FREQ_RANGES, 1};
```
- Add two `Setting` classes that set one side and rebuild from the shadow:
```cpp
class TxFrequencyCap : public Setting {
public:
  explicit TxFrequencyCap(Sa818Context &ctx) : ctx_(ctx) {}
  const FieldSpec &spec() const override { return TXFREQ_SPEC; }
protected:
  Result onSet(const char *value) override {
    if (!ctx_.ready()) return Result::err("driver_error");
    std::optional<float> f = parse_float(value);
    if (!f) return Result::err("bad_value");
    if (!TXFREQ_SPEC.inAnyRange(static_cast<double>(*f))) return Result::err("out_of_range");
    if (sa818_at_set_group(ctx_.dev, ctx_.bw, *f, ctx_.freq_rx, ctx_.tone_tx, ctx_.squelch, ctx_.tone_rx) != SA818_OK)
      return Result::err("driver_error");
    ctx_.freq_tx = *f;
    return Result::okFloat(static_cast<double>(*f));
  }
  Result onGet() override {
    if (!ctx_.ready()) return Result::err("driver_error");
    return Result::okFloat(static_cast<double>(ctx_.freq_tx));
  }
private:
  Sa818Context &ctx_;
};

class RxFrequencyCap : public Setting {
public:
  explicit RxFrequencyCap(Sa818Context &ctx) : ctx_(ctx) {}
  const FieldSpec &spec() const override { return RXFREQ_SPEC; }
protected:
  Result onSet(const char *value) override {
    if (!ctx_.ready()) return Result::err("driver_error");
    std::optional<float> f = parse_float(value);
    if (!f) return Result::err("bad_value");
    if (!RXFREQ_SPEC.inAnyRange(static_cast<double>(*f))) return Result::err("out_of_range");
    if (sa818_at_set_group(ctx_.dev, ctx_.bw, ctx_.freq_tx, *f, ctx_.tone_tx, ctx_.squelch, ctx_.tone_rx) != SA818_OK)
      return Result::err("driver_error");
    ctx_.freq_rx = *f;
    return Result::okFloat(static_cast<double>(*f));
  }
  Result onGet() override {
    if (!ctx_.ready()) return Result::err("driver_error");
    return Result::okFloat(static_cast<double>(ctx_.freq_rx));
  }
private:
  Sa818Context &ctx_;
};
```
- Update every other capability's `sa818_at_set_group(...)` call and `BandwidthCap` to use `ctx_.freq_tx, ctx_.freq_rx, ctx_.tone_tx, …, ctx_.tone_rx` (the shadow no longer has a single `freq`/`tone`). Update `BandwidthCap::onSet` group call to `sa818_at_set_group(ctx_.dev, bw, ctx_.freq_tx, ctx_.freq_rx, ctx_.tone_tx, ctx_.squelch, ctx_.tone_rx)`.
- Add instances + registry entries: `TxFrequencyCap g_txfreq{g_ctx}; RxFrequencyCap g_rxfreq{g_ctx};` and insert `&g_txfreq, &g_rxfreq` into `g_caps` (after `&g_freq`).

Note: this task assumes the shadow's `tone_tx`/`tone_rx` fields exist. Add them now as part of the `Sa818Context` change (they default to `SA818_TONE_NONE`); they are exercised in Task 7. So `Sa818Context` becomes `{dev, bw, freq_tx, freq_rx, tone_tx, tone_rx, squelch}`.

- [ ] **Step 4: Run to verify pass** — twister; repeater tests PASS; existing `frequency` tests still pass; 65/65.

- [ ] **Step 5: Format + commit**

```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation && clang-format-18 -i subsys/module/devices/sa818/sa818_module.cpp && \
git add -A && git commit -m "feat(module): repeater split tx_frequency/rx_frequency

Shadow tracks freq_tx/freq_rx (and tone_tx/tone_rx) independently. New tx_frequency and
rx_frequency settings; frequency stays a simplex convenience (sets both). Refs #36." && git push
```

---

## Task 6: `squelch` capability

**Files:** Modify `subsys/module/devices/sa818/sa818_module.cpp`, `tests/sim_shell/pytest/test_module_iface.py`.

- [ ] **Step 1: Write the failing tests** — append:
```python
def test_module_squelch(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    assert _payload(shell.exec_command("module fm set squelch 3"), "MODULE-RESULT")["ok"]
    assert sa818_sim.get_state().squelch == 3
    assert _payload(shell.exec_command("module fm get squelch"), "MODULE-RESULT")["value"] == 3


def test_module_squelch_out_of_range(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    r = _payload(shell.exec_command("module fm set squelch 9"), "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "out_of_range"
```

- [ ] **Step 2: Run to verify failure** — twister; fails `unknown_capability`.

- [ ] **Step 3: Implement** — in `sa818_module.cpp`:
```cpp
const Range SQUELCH_RANGES[] = {{nullptr, 0.0, 8.0}};
const FieldSpec SQUELCH_SPEC{"squelch", ValueType::Int, nullptr, SQUELCH_RANGES, 1};

class SquelchCap : public Setting {
public:
  explicit SquelchCap(Sa818Context &ctx) : ctx_(ctx) {}
  const FieldSpec &spec() const override { return SQUELCH_SPEC; }
protected:
  Result onSet(const char *value) override {
    if (!ctx_.ready()) return Result::err("driver_error");
    std::optional<long> v = parse_int(value);
    if (!v) return Result::err("bad_value");
    if (!SQUELCH_SPEC.inAnyRange(static_cast<double>(*v))) return Result::err("out_of_range");
    sa818_squelch_level sq = static_cast<sa818_squelch_level>(*v);
    if (sa818_at_set_group(ctx_.dev, ctx_.bw, ctx_.freq_tx, ctx_.freq_rx, ctx_.tone_tx, sq, ctx_.tone_rx) != SA818_OK)
      return Result::err("driver_error");
    ctx_.squelch = sq;
    return Result::okInt(static_cast<int>(*v));
  }
  Result onGet() override {
    if (!ctx_.ready()) return Result::err("driver_error");
    return Result::okInt(static_cast<int>(ctx_.squelch));
  }
private:
  Sa818Context &ctx_;
};
```
Add `SquelchCap g_squelch{g_ctx};` and `&g_squelch` to `g_caps`.

- [ ] **Step 4: Run to verify pass** — twister; 67/67.

- [ ] **Step 5: Format + commit**

```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation && clang-format-18 -i subsys/module/devices/sa818/sa818_module.cpp && \
git add -A && git commit -m "feat(module): squelch (0-8) capability

Refs #36." && git push
```

---

## Task 7: Tones — driver parse/format API + `tx_tone`/`rx_tone`

**Deliverable:** a public tone parse/format API in the driver (single source of truth), used by the human shell and the module; `tx_tone`/`rx_tone` string capabilities.

**Files:** Modify `drivers/radio/sa818/sa818/sa818_at.h`, `drivers/radio/sa818/sa818_at.cpp`, `drivers/radio/sa818/sa818_shell.cpp`, `subsys/module/devices/sa818/sa818_module.cpp`, `tests/sim_shell/pytest/test_module_iface.py`.

**Interfaces:**
- Produces: `enum sa818_tone_code sa818_at_parse_tone(const char *s);` and `int sa818_at_tone_to_str(enum sa818_tone_code code, char *buf, size_t len);`.

- [ ] **Step 1: Write the failing tests** — append:
```python
def test_module_tones(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    assert _payload(shell.exec_command("module fm set tx_tone 67.0"), "MODULE-RESULT")["ok"]
    assert sa818_sim.get_state().ctcss_tx == 1       # CTCSS 67.0 Hz -> code 1
    assert _payload(shell.exec_command("module fm get tx_tone"), "MODULE-RESULT")["value"] == "67.0"
    assert _payload(shell.exec_command("module fm set rx_tone none"), "MODULE-RESULT")["ok"]
    assert sa818_sim.get_state().ctcss_rx == 0
    assert _payload(shell.exec_command("module fm get rx_tone"), "MODULE-RESULT")["value"] == "none"


def test_module_tone_dcs(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    assert _payload(shell.exec_command("module fm set tx_tone 023"), "MODULE-RESULT")["ok"]
    assert sa818_sim.get_state().ctcss_tx == 39      # DCS 023 -> code 39
    assert _payload(shell.exec_command("module fm get tx_tone"), "MODULE-RESULT")["value"] == "023"
```

- [ ] **Step 2: Run to verify failure** — twister; fails `unknown_capability`.

- [ ] **Step 3: Driver — promote tone parse + add reverse** — in `drivers/radio/sa818/sa818/sa818_at.h`, declare:
```cpp
/**
 * @brief Parse a tone string to a code. "none"/"off" -> SA818_TONE_NONE; CTCSS Hz
 * ("67.0".."250.3") -> 1..38; DCS ("023"..) -> 39..121; invalid -> SA818_TONE_NONE.
 */
[[nodiscard]] enum sa818_tone_code sa818_at_parse_tone(const char *s);

/**
 * @brief Format a tone code to its string. 0 -> "none"; 1..38 -> CTCSS Hz; 39..121 -> DCS code.
 * @return bytes written (excl. NUL), or negative on truncation.
 */
int sa818_at_tone_to_str(enum sa818_tone_code code, char *buf, size_t len);
```
In `sa818_at.cpp`, implement both. Move the CTCSS-frequency table + parsing logic from `sa818_shell.cpp`'s `parse_tone` into `sa818_at_parse_tone` (verbatim logic). Build a bidirectional CTCSS table `{code, "67.0"}` … and a DCS table `{code, "023"}` … (DCS codes are the standard set encoded 39..121; enumerate the same list the header documents). `sa818_at_tone_to_str` looks up `code` in those tables (`0` → `"none"`).
In `sa818_shell.cpp`, delete the static `parse_tone` and call `sa818_at_parse_tone` where it was used (`cmd_sa818_at_group`). No behavior change.

- [ ] **Step 4: SA818 module — tone capabilities** — in `sa818_module.cpp`:
```cpp
const FieldSpec TXTONE_SPEC{"tx_tone", ValueType::String};
const FieldSpec RXTONE_SPEC{"rx_tone", ValueType::String};

class TxToneCap : public Setting {
public:
  explicit TxToneCap(Sa818Context &ctx) : ctx_(ctx) {}
  const FieldSpec &spec() const override { return TXTONE_SPEC; }
protected:
  Result onSet(const char *value) override {
    if (!ctx_.ready()) return Result::err("driver_error");
    sa818_tone_code t = sa818_at_parse_tone(value);
    if (sa818_at_set_group(ctx_.dev, ctx_.bw, ctx_.freq_tx, ctx_.freq_rx, t, ctx_.squelch, ctx_.tone_rx) != SA818_OK)
      return Result::err("driver_error");
    ctx_.tone_tx = t;
    char b[16];
    sa818_at_tone_to_str(t, b, sizeof(b));
    return Result::okStr(b);
  }
  Result onGet() override {
    if (!ctx_.ready()) return Result::err("driver_error");
    char b[16];
    sa818_at_tone_to_str(ctx_.tone_tx, b, sizeof(b));
    return Result::okStr(b);
  }
private:
  Sa818Context &ctx_;
};
```
Add an analogous `RxToneCap` (sets `tone_rx`, group call uses `ctx_.tone_tx, ctx_.squelch, t`, gets `ctx_.tone_rx`). Add instances + `&g_txtone, &g_rxtone` to `g_caps`.

**Important — `Result::okStr` lifetime:** `okStr` stores the pointer; here it points at the local `b[16]`. Since `Result` is rendered within the same `execute → emit_result` stack frame *before* `onSet`/`onGet` returns up… no — `onSet` returns `Result` by value and `b` goes out of scope. Fix: make the tone-capability result own its string. Add a small owned-string result to the framework: extend `Result` with an inline `char sbuf_[16]` used by a new `Result::okStrCopy(const char*)` that copies into `sbuf_` and points `s_` at it. Use `okStrCopy` for tones. (Enum caps that return string literals keep using `okStr`.) Implement `okStrCopy` in `iface.h`:
```cpp
  static Result okStrCopy(const char *v) {
    Result r(true);
    r.vk_ = VK::Str;
    size_t i = 0;
    for (; v != nullptr && v[i] != '\0' && i + 1 < sizeof(r.sbuf_); ++i) {
      r.sbuf_[i] = v[i];
    }
    r.sbuf_[i] = '\0';
    r.s_ = r.sbuf_;
    return r;
  }
```
and add member `char sbuf_[16] = {0};`. `Result` stays trivially copyable-enough (no heap); copying `sbuf_` and re-pointing `s_` on copy is NOT automatic — so also give `Result` a copy constructor that re-points `s_` to the copy's own `sbuf_` when `s_ == &other.sbuf_[0]`:
```cpp
  Result(const Result &o) { *this = o; }
  Result &operator=(const Result &o) {
    ok_ = o.ok_; vk_ = o.vk_; i_ = o.i_; f_ = o.f_; b_ = o.b_; err_ = o.err_;
    for (size_t k = 0; k < sizeof(sbuf_); ++k) sbuf_[k] = o.sbuf_[k];
    s_ = (o.s_ == o.sbuf_) ? sbuf_ : o.s_;
    return *this;
  }
```
Use `Result::okStrCopy(b)` in the tone `onSet`/`onGet`.

- [ ] **Step 5: Run to verify pass** — twister; tone tests PASS; existing `sa818 at group` tests still pass; 69/69.

- [ ] **Step 6: Format + commit**

```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation && clang-format-18 -i drivers/radio/sa818/sa818_shell.cpp subsys/module/devices/sa818/sa818_module.cpp include/oe5xrx/module/iface.h && \
git add -A && git commit -m "feat(module): tx_tone/rx_tone + promote tone parse/format to driver API

New sa818_at_parse_tone/sa818_at_tone_to_str in the driver AT API (single source of truth,
sa818 shell refactored to use it). tx_tone/rx_tone string capabilities (none/CTCSS Hz/DCS).
Result gains an owned-string variant for the formatted tone. Refs #36." && git push
```

---

## Task 8: `band` telemetry

**Deliverable:** readonly `band` reports the active range name for the current RX frequency.

**Files:** Modify `subsys/module/devices/sa818/sa818_module.cpp`, `tests/sim_shell/pytest/test_module_iface.py`.

- [ ] **Step 1: Write the failing test** — append:
```python
def test_module_band_telemetry(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    shell.exec_command("module fm set rx_frequency 145.500")
    r = _payload(shell.exec_command("module fm get band"), "MODULE-RESULT")
    assert r["ok"] is True and r["value"] == "vhf"
```
Also assert `band` appears in describe as telemetry:
```python
    assert caps["band"]["kind"] == "telemetry"
    assert caps["band"]["type"] == "string"
```

- [ ] **Step 2: Run to verify failure** — twister; fails.

- [ ] **Step 3: Implement** — in `sa818_module.cpp`:
```cpp
const FieldSpec BAND_SPEC{"band", ValueType::String, nullptr, nullptr, 0, nullptr, 0, /*readonly=*/true};

class BandCap : public Telemetry {
public:
  explicit BandCap(Sa818Context &ctx) : ctx_(ctx) {}
  const FieldSpec &spec() const override { return BAND_SPEC; }
protected:
  Result onGet() override {
    if (!ctx_.ready()) return Result::err("driver_error");
    double f = static_cast<double>(ctx_.freq_rx);
    for (size_t i = 0; i < FREQ_SPEC.rangeCount; ++i) {
      if (FREQ_SPEC.ranges[i].name != nullptr && f >= FREQ_SPEC.ranges[i].min && f <= FREQ_SPEC.ranges[i].max) {
        return Result::okStr(FREQ_SPEC.ranges[i].name); // range name is a static literal
      }
    }
    return Result::okNull();
  }
private:
  Sa818Context &ctx_;
};
```
Add `Result::okNull()` to `iface.h` (renders `null` — reuse the `VK::None` success path, which `renderValue` already prints as `null`):
```cpp
  static Result okNull() {
    Result r(true);
    r.vk_ = VK::None;
    return r;
  }
```
Add `BandCap g_band{g_ctx};` and `&g_band` to `g_caps`.

- [ ] **Step 4: Run to verify pass** — twister; band test PASS; 71/71.

- [ ] **Step 5: Format + commit**

```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation && clang-format-18 -i subsys/module/devices/sa818/sa818_module.cpp include/oe5xrx/module/iface.h && \
git add -A && git commit -m "feat(module): band telemetry (active named range for current RX freq)

Refs #36." && git push
```

---

## Task 9: Verification, review, Copilot

- [ ] **App parity + full clang-format (CI parity)**
```bash
cd /home/pbuchegger/OE5XRX && . /home/pbuchegger/OE5XRX/.zephyr-venv/bin/activate && export ZEPHYR_BASE=/home/pbuchegger/OE5XRX/zephyr ZEPHYR_TOOLCHAIN_VARIANT=host && cd FW-RemoteStation && west build -b native_sim/native/64 app -p always 2>&1 | tail -2
cd /home/pbuchegger/OE5XRX/FW-RemoteStation && bash -c 'mapfile -t F < <(find app boards tests -type f \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" \)); clang-format-18 -i "${F[@]}"; git diff --quiet && echo FMT-OK || git --no-pager diff --name-only'
```
- [ ] **Round 2 — Watchers:** `atlas` (spec-compliance vs the 2026-07-02 spec §2–§9 + meta-spec §4/§5.1/§5.2/§8) and `audit` (C++/Zephyr quality: no heap/exceptions/RTTI, `Result` owned-string lifetime, static-init order, `std::span` registry, DT variant) on the full branch diff. Address findings.
- [ ] **Integration (`probe`) — full E2E:** independently rebuild + confirm `module fm describe` (ranges + band + model/version), repeater split → `sim.freq_tx/freq_rx`, tones → `sim.ctcss_tx/rx`, squelch → `sim.squelch`, `module list`, `unknown_module`; existing `sa818` shell tree unbroken.
- [ ] **verification-before-completion:** full `sim_shell` suite 100% + app build + clang-format; paste evidence.
- [ ] **Copilot loop** (copilot-loop skill) until converged; address findings.

---

## Self-Review (against the spec)

- §2 restructure → Task 1. ✓  §3.1 ranges → Task 2. ✓  §3.2/§3.3 multi-module/grammar → Task 3. ✓
- §4 variant → Task 4. ✓  §5 capabilities: frequency (T2), tx/rx (T5), squelch (T6), tones (T7), band (T8), rssi raw (T2), ptt/power/volume/bandwidth (carried from D1). ✓
- §7 tone driver API → Task 7. ✓  §8 tests → each task + Task 9. ✓  §9 wire contract (`module` field, error codes, `null`, `too_long`) → T3 + carried. ✓
- Guardrails (no heap/exceptions/RTTI; std::optional/span/constexpr; thin firmware; clang-format) → Global Constraints + every task. ✓
