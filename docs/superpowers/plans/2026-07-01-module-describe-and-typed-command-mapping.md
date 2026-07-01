# D1: Generic `module describe` + Typed Command Mapping (SA818) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a thin, generic, machine-readable `module` shell interface to the firmware that maps onto the existing SA818 driver, forming the firmware half of the Firmware↔Agent contract (meta-spec §8).

**Architecture:** A new translation unit `drivers/radio/sa818/sa818_module.cpp` registers a `module` Zephyr shell group (`describe`/`set`/`get`/`do`) beside the untouched `sa818` tree. A static capability table drives `describe`; validated `{cap, op, value}` commands dispatch to the existing SA818 driver API. Output is one token-prefixed compact-JSON line per response. Everything runs on `native_sim`.

**Tech Stack:** Zephyr RTOS (C++20), Zephyr shell subsystem, `native_sim/native/64`, pytest + `twister_harness` (PTY-based `SA818Simulator`).

**Spec:** `docs/superpowers/specs/2026-07-01-module-describe-and-typed-command-mapping-design.md`
**Meta-spec (contract):** `../station-manager/docs/superpowers/specs/2026-06-21-module-platform-sim-bridge-design.md` §5.1, §5.2, §8, §9.

## Global Constraints

- **Firmware stays thin (§5.2):** no platform knowledge, no capability persistence, no access model. The RAM group-shadow is working state only.
- **Do not break** the existing `sa818` shell tree or the SA818 driver API. Only allowed driver-dir change beyond the new file: the one-line Kconfig fix in Task 1.
- **One feature branch, one PR** (`feature/d1-module-describe`, PR #38). Commit after every task; push after every task.
- **clang-format must pass.** Format `.c/.h/.cpp/.hpp` under `app`/`boards`/`tests` with `clang-format-18` (CI checks these paths). Also format the new `drivers/.../sa818_module.cpp` to the repo `.clang-format` even though CI does not check `drivers/`.
- **Wire-format tokens are contract:** describe line begins `MODULE-DESCRIBE `, command results begin `MODULE-RESULT `, each followed by one line of compact JSON. Error codes: `unknown_capability`, `bad_value`, `out_of_range`, `read_only`, `wrong_op`, `driver_error`, `usage`.
- **Identity:** `type=fm_transceiver`, `model=SA818-V`, `version=2m`.

### Build / test environment (already provisioned in this session)

A Zephyr west workspace is set up at `/home/pbuchegger/OE5XRX` with a Python venv. Every build/test command runs as:

```bash
cd /home/pbuchegger/OE5XRX && . .zephyr-venv/bin/activate && \
  export ZEPHYR_BASE=/home/pbuchegger/OE5XRX/zephyr ZEPHYR_TOOLCHAIN_VARIANT=host && \
  cd FW-RemoteStation && <west command>
```

- Full sim_shell suite: `west twister -T tests/sim_shell -p native_sim/native/64 -c --inline-logs`
  (the `-c`/clobber is REQUIRED after any Kconfig/prj.conf/source change — twister otherwise reuses the cached build).
- Single pytest scenario, faster iteration after a build exists: use `west twister ... -c` then read `twister-out/.../handler.log`. Twister runs the whole `pytest_root`; there is no per-test twister filter, so scope assertions live in the test file.
- App build parity with CI: `west build -b native_sim/native/64 app -p always`.

---

## File Structure

- **Create** `drivers/radio/sa818/sa818_module.cpp` — the generic `module` interface (capability table, describe, validate + dispatch, JSON emit). The single module-specific firmware piece.
- **Create** `tests/sim_shell/pytest/test_module_iface.py` — pytest E2E for the `module` interface.
- **Modify** `drivers/radio/sa818/Kconfig` — fix `depends on UART`→`depends on SERIAL`; add `config SA818_MODULE_IFACE`.
- **Modify** `drivers/radio/sa818/CMakeLists.txt` — compile `sa818_module.cpp` when `CONFIG_SA818_MODULE_IFACE`.
- **Modify** `tests/sim_shell/prj.conf` — enable `CONFIG_SA818_SHELL=y` and `CONFIG_SA818_MODULE_IFACE=y`.
- **Modify** `tests/sim_shell/pytest/test_sim_shell.py` — Task 6: point the long-`tmp_path` wav test at a short path.
- **Modify** `.github/workflows/ci.yml` — Task 6: enable the `tests/sim_shell` twister job.

---

## Task 1: Buildable driver + `module describe`

Foundation task: make the SA818 driver compile, add the `module` command group with a working `describe`, and un-break the pre-existing sa818 tests as a side effect.

**Files:**
- Modify: `drivers/radio/sa818/Kconfig`
- Modify: `drivers/radio/sa818/CMakeLists.txt`
- Modify: `tests/sim_shell/prj.conf`
- Create: `drivers/radio/sa818/sa818_module.cpp`
- Create/Test: `tests/sim_shell/pytest/test_module_iface.py`

**Interfaces:**
- Consumes: existing driver API `sa818.h` (`sa818_get_status`, enums) and `sa818_at.h` (`sa818_at_set_group`, `sa818_at_read_rssi`, `sa818_at_set_volume`, enums). Device handle via `DEVICE_DT_GET_OR_NULL(DT_NODELABEL(sa818))`.
- Produces (used by later tasks, same file): `do_set()`, `do_get()`, `do_do()` dispatchers; helpers `result_ok_num()`, `result_ok_bool()`, `result_err()`; `struct group_shadow g_shadow` with fields `sa818_bandwidth bw`, `float freq`, `sa818_tone_code tone`, `sa818_squelch_level squelch`.

- [ ] **Step 1: Write the failing test** — `tests/sim_shell/pytest/test_module_iface.py`

```python
# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Generic `module` interface tests (describe + typed command mapping) on native_sim."""
import json
import re

_ANSI = re.compile(r"\x1b\[[0-9;]*m")


def _lines(out):
    lines = out if isinstance(out, list) else str(out).splitlines()
    return [_ANSI.sub("", l).strip() for l in lines]


def _payload(out, token):
    """Extract and json.loads the compact-JSON body of the first `token ` line."""
    needle = token + " "
    for l in _lines(out):
        i = l.find(needle)
        if i != -1:
            return json.loads(l[i + len(needle):])
    raise AssertionError(f"no {token} line in output: {_lines(out)}")


def test_module_describe_valid_json(shell):
    out = shell.exec_command("module describe")
    d = _payload(out, "MODULE-DESCRIBE")

    assert d["schema"] == 1
    assert d["identity"] == {"type": "fm_transceiver", "model": "SA818-V", "version": "2m"}

    caps = {c["name"]: c for c in d["capabilities"]}
    assert set(caps) == {"frequency", "ptt", "power_level", "rssi", "volume", "bandwidth"}

    assert caps["frequency"]["kind"] == "setting"
    assert caps["frequency"]["type"] == "float"
    assert caps["frequency"]["unit"] == "MHz"
    assert caps["frequency"]["min"] == 144.0
    assert caps["frequency"]["max"] == 148.0

    assert caps["ptt"]["kind"] == "action"
    assert caps["ptt"]["type"] == "bool"

    assert caps["power_level"]["kind"] == "setting"
    assert caps["power_level"]["type"] == "enum"
    assert caps["power_level"]["values"] == ["low", "high"]

    assert caps["rssi"]["kind"] == "telemetry"
    assert caps["rssi"]["type"] == "int"
    assert caps["rssi"]["readonly"] is True

    assert caps["volume"]["type"] == "int"
    assert caps["volume"]["min"] == 1
    assert caps["volume"]["max"] == 8

    assert caps["bandwidth"]["type"] == "enum"
    assert caps["bandwidth"]["values"] == ["12.5", "25"]
```

- [ ] **Step 2: Fix the driver Kconfig so it can build** — `drivers/radio/sa818/Kconfig`

Change `depends on UART` to `depends on SERIAL` (there is no bare `CONFIG_UART` symbol in Zephyr; on `main` this forces `CONFIG_SA818=n` and the driver never compiles). Then add the new symbol inside the `if SA818` block:

```kconfig
config SA818_MODULE_IFACE
  bool "SA818 generic module interface (describe + typed commands)"
  default n
  depends on SHELL
  help
    Registers the generic, machine-readable `module` shell command group
    (describe / set / get / do) mapping onto the SA818 driver. This is the
    Firmware<->Agent contract surface; the human `sa818` command tree stays separate.
```

- [ ] **Step 3: Compile the new source when enabled** — `drivers/radio/sa818/CMakeLists.txt`

Append after the existing `CONFIG_SA818_SHELL` block:

```cmake
zephyr_library_sources_ifdef(
  CONFIG_SA818_MODULE_IFACE
  sa818_module.cpp)
```

- [ ] **Step 4: Enable the interface in the test config** — append to `tests/sim_shell/prj.conf`

```conf
# SA818 human shell (required by the existing sa818 pytest tests)
CONFIG_SA818_SHELL=y

# Generic module interface under test (describe + typed commands)
CONFIG_SA818_MODULE_IFACE=y
```

- [ ] **Step 5: Create `drivers/radio/sa818/sa818_module.cpp`** with the capability table + `describe` (dispatch stubs return `usage`/`unknown_capability` for now; filled in later tasks)

```cpp
/**
 * @file sa818_module.cpp
 * @brief Generic machine-readable module interface for the SA818.
 *
 * Registers a `module` shell command group (describe / set / get / do) that maps
 * the generic {capability, op, value} contract onto the SA818 driver. This is the
 * firmware half of the Firmware<->Agent contract (module-platform meta-spec §8).
 * The human `sa818` command tree stays separate and untouched.
 *
 * @copyright Copyright (c) 2025 OE5XRX
 * @spdx-license-identifier LGPL-3.0-or-later
 */

#ifdef CONFIG_SA818_MODULE_IFACE

#include <sa818/sa818.h>
#include <sa818/sa818_at.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

namespace {

const struct device *sa818_dev(void) { return DEVICE_DT_GET_OR_NULL(DT_NODELABEL(sa818)); }

/* RAM shadow of the group config so `set frequency` / `set bandwidth` can rebuild the
 * full sa818_at_set_group() call. Seeded to the module's power-on defaults (matching the
 * SA818Simulator defaults). Working state only -- NOT capability persistence. */
struct group_shadow {
  sa818_bandwidth bw;
  float freq;
  sa818_tone_code tone;
  sa818_squelch_level squelch;
};
group_shadow g_shadow = {SA818_BW_12_5_KHZ, 145.500f, SA818_TONE_NONE, SA818_SQL_LEVEL_4};

/* Descriptor fragments -- single source of truth for `module describe`. */
struct capability {
  const char *name;
  const char *json;
};
const capability CAPS[] = {
    {"frequency",
     "{\"name\":\"frequency\",\"kind\":\"setting\",\"type\":\"float\",\"unit\":\"MHz\",\"min\":144.0,\"max\":148.0,\"step\":0.0125,\"access\":\"operator\"}"},
    {"ptt", "{\"name\":\"ptt\",\"kind\":\"action\",\"type\":\"bool\",\"access\":\"operator\"}"},
    {"power_level",
     "{\"name\":\"power_level\",\"kind\":\"setting\",\"type\":\"enum\",\"values\":[\"low\",\"high\"],\"access\":\"operator\"}"},
    {"rssi",
     "{\"name\":\"rssi\",\"kind\":\"telemetry\",\"type\":\"int\",\"unit\":\"dBm\",\"readonly\":true,\"access\":\"operator\"}"},
    {"volume", "{\"name\":\"volume\",\"kind\":\"setting\",\"type\":\"int\",\"min\":1,\"max\":8,\"access\":\"operator\"}"},
    {"bandwidth",
     "{\"name\":\"bandwidth\",\"kind\":\"setting\",\"type\":\"enum\",\"values\":[\"12.5\",\"25\"],\"unit\":\"kHz\",\"access\":\"operator\"}"},
};

void result_err(const struct shell *sh, const char *cap, const char *op, const char *err) {
  shell_print(sh, "MODULE-RESULT {\"ok\":false,\"cap\":\"%s\",\"op\":\"%s\",\"error\":\"%s\"}", cap, op, err);
}

void result_ok_int(const struct shell *sh, const char *cap, const char *op, int value) {
  shell_print(sh, "MODULE-RESULT {\"ok\":true,\"cap\":\"%s\",\"op\":\"%s\",\"value\":%d}", cap, op, value);
}

void result_ok_float(const struct shell *sh, const char *cap, const char *op, double value) {
  shell_print(sh, "MODULE-RESULT {\"ok\":true,\"cap\":\"%s\",\"op\":\"%s\",\"value\":%g}", cap, op, value);
}

void result_ok_bool(const struct shell *sh, const char *cap, const char *op, bool value) {
  shell_print(sh, "MODULE-RESULT {\"ok\":true,\"cap\":\"%s\",\"op\":\"%s\",\"value\":%s}", cap, op, value ? "true" : "false");
}

void result_ok_str(const struct shell *sh, const char *cap, const char *op, const char *value) {
  shell_print(sh, "MODULE-RESULT {\"ok\":true,\"cap\":\"%s\",\"op\":\"%s\",\"value\":\"%s\"}", cap, op, value);
}

void emit_describe(const struct shell *sh) {
  static char buf[1024];
  int n = snprintf(buf, sizeof(buf),
                   "MODULE-DESCRIBE {\"schema\":1,\"identity\":{\"type\":\"fm_transceiver\",\"model\":\"SA818-V\",\"version\":"
                   "\"2m\"},\"capabilities\":[");
  for (size_t i = 0; i < ARRAY_SIZE(CAPS); ++i) {
    n += snprintf(buf + n, sizeof(buf) - n, "%s%s", i ? "," : "", CAPS[i].json);
  }
  snprintf(buf + n, sizeof(buf) - n, "]}");
  shell_print(sh, "%s", buf);
}

/* Dispatchers -- fleshed out in later tasks. */
int do_set(const struct shell *sh, const char *cap, const char *valstr);
int do_get(const struct shell *sh, const char *cap);
int do_do(const struct shell *sh, const char *cap, const char *valstr);

int do_set(const struct shell *sh, const char *cap, const char *) {
  result_err(sh, cap, "set", "unknown_capability");
  return 0;
}
int do_get(const struct shell *sh, const char *cap) {
  result_err(sh, cap, "get", "unknown_capability");
  return 0;
}
int do_do(const struct shell *sh, const char *cap, const char *) {
  result_err(sh, cap, "do", "unknown_capability");
  return 0;
}

int cmd_module_describe(const struct shell *sh, size_t, char **) {
  emit_describe(sh);
  return 0;
}

int cmd_module_set(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 3) {
    result_err(sh, argc >= 2 ? argv[1] : "", "set", "usage");
    return 0;
  }
  return do_set(sh, argv[1], argv[2]);
}

int cmd_module_get(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 2) {
    result_err(sh, "", "get", "usage");
    return 0;
  }
  return do_get(sh, argv[1]);
}

int cmd_module_do(const struct shell *sh, size_t argc, char **argv) {
  if (argc < 3) {
    result_err(sh, argc >= 2 ? argv[1] : "", "do", "usage");
    return 0;
  }
  return do_do(sh, argv[1], argv[2]);
}

} // namespace

// clang-format off
SHELL_STATIC_SUBCMD_SET_CREATE(
    module_cmds,
    SHELL_CMD_ARG(describe, NULL, "Emit machine-readable module descriptor", cmd_module_describe, 1, 0),
    SHELL_CMD_ARG(set, NULL, "set <capability> <value>", cmd_module_set, 3, 0),
    SHELL_CMD_ARG(get, NULL, "get <capability>", cmd_module_get, 2, 0),
    SHELL_CMD_ARG(do, NULL, "do <capability> <value>", cmd_module_do, 3, 0),
    SHELL_SUBCMD_SET_END);
// clang-format on

SHELL_CMD_REGISTER(module, &module_cmds, "Generic module interface (describe/set/get/do)", NULL);

#endif /* CONFIG_SA818_MODULE_IFACE */
```

- [ ] **Step 6: Build + run — verify the describe test passes and no sa818 test regressed**

```bash
cd /home/pbuchegger/OE5XRX && . .zephyr-venv/bin/activate && \
  export ZEPHYR_BASE=/home/pbuchegger/OE5XRX/zephyr ZEPHYR_TOOLCHAIN_VARIANT=host && \
  cd FW-RemoteStation && \
  west twister -T tests/sim_shell -p native_sim/native/64 -c --inline-logs 2>&1 | \
  grep -E "test_module_describe_valid_json|executed test cases passed|scenario\(s\) failed"
```
Expected: `test_module_describe_valid_json` PASSED; `39 of 41` (or similar) pass — the only failure is the pre-existing `test_wav_load_start_adc` (fixed in Task 6). No `sa818: command not found`.

- [ ] **Step 7: Format the new source**

```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation && clang-format-18 -i drivers/radio/sa818/sa818_module.cpp && git diff --stat
```

- [ ] **Step 8: Commit**

```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation && \
git add drivers/radio/sa818/Kconfig drivers/radio/sa818/CMakeLists.txt tests/sim_shell/prj.conf \
        drivers/radio/sa818/sa818_module.cpp tests/sim_shell/pytest/test_module_iface.py && \
git commit -m "feat(module): buildable SA818 driver + \`module describe\`

Fix Kconfig depends UART->SERIAL so the driver compiles; add CONFIG_SA818_MODULE_IFACE
and the generic \`module\` shell group emitting a machine-readable MODULE-DESCRIBE line.
Enabling SA818_SHELL also un-breaks the pre-existing sa818 pytest suite.

Refs #36." && git push
```

---

## Task 2: `module set frequency` — the end-to-end proof (§8)

**Files:**
- Modify: `drivers/radio/sa818/sa818_module.cpp` (implement `frequency` branch of `do_set`)
- Modify: `tests/sim_shell/pytest/test_module_iface.py`

**Interfaces:**
- Consumes: `sa818_at_set_group(dev, bw, tx, rx, tx_tone, squelch, rx_tone)`; `g_shadow`; `result_ok_float`, `result_err`.
- Produces: validated `frequency` set path (range [144.0, 148.0]).

- [ ] **Step 1: Write the failing tests** — append to `test_module_iface.py`

```python
def test_module_set_frequency_e2e(sa818_sim, shell):
    """The §8 datapoint: typed set flows through the real driver to the module."""
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module set frequency 145.500")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is True
    assert r["cap"] == "frequency"
    assert r["op"] == "set"
    assert r["value"] == 145.5
    # Verify it actually drove the SA818 driver -> AT+DMOSETGROUP -> simulator state.
    assert sa818_sim.get_state().freq_tx == 145.5
    assert sa818_sim.get_state().freq_rx == 145.5


def test_module_set_frequency_out_of_range(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    shell.exec_command("module set frequency 145.500")
    out = shell.exec_command("module set frequency 150.0")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False
    assert r["error"] == "out_of_range"
    # Rejected value did not reach the module.
    assert sa818_sim.get_state().freq_tx == 145.5


def test_module_set_frequency_bad_value(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module set frequency abc")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False
    assert r["error"] == "bad_value"
```

- [ ] **Step 2: Run to verify failure**

```bash
cd /home/pbuchegger/OE5XRX && . .zephyr-venv/bin/activate && \
  export ZEPHYR_BASE=/home/pbuchegger/OE5XRX/zephyr ZEPHYR_TOOLCHAIN_VARIANT=host && \
  cd FW-RemoteStation && west twister -T tests/sim_shell -p native_sim/native/64 -c --inline-logs 2>&1 | \
  grep -E "test_module_set_frequency"
```
Expected: the three new tests FAIL (result is `unknown_capability`, not the expected values).

- [ ] **Step 3: Implement the `frequency` branch** — replace the stub `do_set` in `sa818_module.cpp` with:

```cpp
int do_set(const struct shell *sh, const char *cap, const char *valstr) {
  const struct device *dev = sa818_dev();
  if (!dev || !device_is_ready(dev)) {
    result_err(sh, cap, "set", "driver_error");
    return 0;
  }

  if (!strcmp(cap, "frequency")) {
    char *end = nullptr;
    float f = strtof(valstr, &end);
    if (end == valstr || *end != '\0') {
      result_err(sh, cap, "set", "bad_value");
      return 0;
    }
    if (f < 144.0f || f > 148.0f) {
      result_err(sh, cap, "set", "out_of_range");
      return 0;
    }
    g_shadow.freq = f;
    sa818_result r = sa818_at_set_group(dev, g_shadow.bw, f, f, g_shadow.tone, g_shadow.squelch, g_shadow.tone);
    if (r != SA818_OK) {
      result_err(sh, cap, "set", "driver_error");
      return 0;
    }
    result_ok_float(sh, cap, "set", (double)f);
    return 0;
  }

  result_err(sh, cap, "set", "unknown_capability");
  return 0;
}
```

- [ ] **Step 4: Run to verify pass**

```bash
cd /home/pbuchegger/OE5XRX && . .zephyr-venv/bin/activate && \
  export ZEPHYR_BASE=/home/pbuchegger/OE5XRX/zephyr ZEPHYR_TOOLCHAIN_VARIANT=host && \
  cd FW-RemoteStation && west twister -T tests/sim_shell -p native_sim/native/64 -c --inline-logs 2>&1 | \
  grep -E "test_module_set_frequency|scenario\(s\) failed"
```
Expected: all three `test_module_set_frequency_*` PASS.

- [ ] **Step 5: Format + commit**

```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation && clang-format-18 -i drivers/radio/sa818/sa818_module.cpp && \
git add drivers/radio/sa818/sa818_module.cpp tests/sim_shell/pytest/test_module_iface.py && \
git commit -m "feat(module): typed \`set frequency\` end-to-end to SA818 driver

Validate float range 144.0-148.0, rebuild the group config from the RAM shadow, and
drive sa818_at_set_group(). Verified end-to-end: SA818Simulator.freq_tx == 145.5 (§8).

Refs #36." && git push
```

---

## Task 3: Remaining set/do mappings — ptt, power_level, volume, bandwidth

**Files:**
- Modify: `drivers/radio/sa818/sa818_module.cpp` (extend `do_set`, implement `do_do`)
- Modify: `tests/sim_shell/pytest/test_module_iface.py`

**Interfaces:**
- Consumes: `sa818_set_ptt`, `sa818_set_power_level`, `sa818_at_set_volume`, `sa818_get_status`, `sa818_at_set_group`; enums from `sa818.h`/`sa818_at.h`.
- Produces: value parsing helpers `parse_bool()`; full `set` coverage for `power_level`, `volume`, `bandwidth`; `do ptt`.

- [ ] **Step 1: Write the failing tests** — append to `test_module_iface.py`

```python
def _status(shell):
    out = shell.exec_command("sa818 status")
    text = "\n".join(_lines(out))
    m = re.search(r"powered=(\d+)\s+ptt=(\d+)\s+high_power=(\d+)\s+squelch=(\d+)\s+volume=(\d+)", text)
    assert m, f"could not parse sa818 status: {text}"
    return {"powered": int(m[1]), "ptt": int(m[2]), "high_power": int(m[3]),
            "squelch": int(m[4]), "volume": int(m[5])}


def test_module_do_ptt(shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module do ptt on")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is True and r["value"] is True
    assert _status(shell)["ptt"] == 1

    out = shell.exec_command("module do ptt off")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is True and r["value"] is False
    assert _status(shell)["ptt"] == 0


def test_module_do_ptt_bad_value(shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module do ptt maybe")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "bad_value"


def test_module_set_power_level(shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module set power_level high")
    assert _payload(out, "MODULE-RESULT")["ok"] is True
    assert _status(shell)["high_power"] == 1
    out = shell.exec_command("module set power_level low")
    assert _payload(out, "MODULE-RESULT")["ok"] is True
    assert _status(shell)["high_power"] == 0


def test_module_set_power_level_bad_value(shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module set power_level medium")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "bad_value"


def test_module_set_volume(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module set volume 7")
    assert _payload(out, "MODULE-RESULT")["ok"] is True
    assert sa818_sim.get_state().volume == 7


def test_module_set_volume_out_of_range(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module set volume 9")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "out_of_range"


def test_module_set_bandwidth(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module set bandwidth 25")
    assert _payload(out, "MODULE-RESULT")["ok"] is True
    assert sa818_sim.get_state().bandwidth == 1
    out = shell.exec_command("module set bandwidth 12.5")
    assert _payload(out, "MODULE-RESULT")["ok"] is True
    assert sa818_sim.get_state().bandwidth == 0
```

- [ ] **Step 2: Run to verify failure**

Run the twister command from Task 2 Step 2, grepping `test_module_do_ptt|test_module_set_power_level|test_module_set_volume|test_module_set_bandwidth`. Expected: new tests FAIL.

- [ ] **Step 3: Implement.** Add a bool parser above `do_set`, extend `do_set`, and implement `do_do`.

Add helper in the anonymous namespace (before `do_set`):

```cpp
/* Parse on/off/1/0/true/false. Returns true on success and writes *out. */
bool parse_bool(const char *s, bool *out) {
  if (!strcmp(s, "on") || !strcmp(s, "1") || !strcmp(s, "true")) {
    *out = true;
    return true;
  }
  if (!strcmp(s, "off") || !strcmp(s, "0") || !strcmp(s, "false")) {
    *out = false;
    return true;
  }
  return false;
}
```

Extend `do_set` — insert these branches before the final `unknown_capability` return:

```cpp
  if (!strcmp(cap, "power_level")) {
    if (!strcmp(valstr, "high")) {
      if (sa818_set_power_level(dev, SA818_POWER_HIGH) != SA818_OK) {
        result_err(sh, cap, "set", "driver_error");
        return 0;
      }
    } else if (!strcmp(valstr, "low")) {
      if (sa818_set_power_level(dev, SA818_POWER_LOW) != SA818_OK) {
        result_err(sh, cap, "set", "driver_error");
        return 0;
      }
    } else {
      result_err(sh, cap, "set", "bad_value");
      return 0;
    }
    result_ok_str(sh, cap, "set", valstr);
    return 0;
  }

  if (!strcmp(cap, "volume")) {
    char *end = nullptr;
    long v = strtol(valstr, &end, 10);
    if (end == valstr || *end != '\0') {
      result_err(sh, cap, "set", "bad_value");
      return 0;
    }
    if (v < 1 || v > 8) {
      result_err(sh, cap, "set", "out_of_range");
      return 0;
    }
    if (sa818_at_set_volume(dev, static_cast<sa818_volume_level>(v)) != SA818_OK) {
      result_err(sh, cap, "set", "driver_error");
      return 0;
    }
    result_ok_int(sh, cap, "set", (int)v);
    return 0;
  }

  if (!strcmp(cap, "bandwidth")) {
    sa818_bandwidth bw;
    if (!strcmp(valstr, "12.5")) {
      bw = SA818_BW_12_5_KHZ;
    } else if (!strcmp(valstr, "25")) {
      bw = SA818_BW_25_KHZ;
    } else {
      result_err(sh, cap, "set", "bad_value");
      return 0;
    }
    g_shadow.bw = bw;
    if (sa818_at_set_group(dev, g_shadow.bw, g_shadow.freq, g_shadow.freq, g_shadow.tone, g_shadow.squelch,
                           g_shadow.tone) != SA818_OK) {
      result_err(sh, cap, "set", "driver_error");
      return 0;
    }
    result_ok_str(sh, cap, "set", valstr);
    return 0;
  }
```

Replace the stub `do_do` with:

```cpp
int do_do(const struct shell *sh, const char *cap, const char *valstr) {
  const struct device *dev = sa818_dev();
  if (!dev || !device_is_ready(dev)) {
    result_err(sh, cap, "do", "driver_error");
    return 0;
  }

  if (!strcmp(cap, "ptt")) {
    bool on;
    if (!parse_bool(valstr, &on)) {
      result_err(sh, cap, "do", "bad_value");
      return 0;
    }
    if (sa818_set_ptt(dev, on ? SA818_PTT_ON : SA818_PTT_OFF) != SA818_OK) {
      result_err(sh, cap, "do", "driver_error");
      return 0;
    }
    result_ok_bool(sh, cap, "do", on);
    return 0;
  }

  result_err(sh, cap, "do", "unknown_capability");
  return 0;
}
```

- [ ] **Step 4: Run to verify pass** — same twister command; all new tests PASS.

- [ ] **Step 5: Format + commit**

```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation && clang-format-18 -i drivers/radio/sa818/sa818_module.cpp && \
git add drivers/radio/sa818/sa818_module.cpp tests/sim_shell/pytest/test_module_iface.py && \
git commit -m "feat(module): map ptt/power_level/volume/bandwidth onto SA818 driver

Refs #36." && git push
```

---

## Task 4: `get` (telemetry + current values) and error paths

**Files:**
- Modify: `drivers/radio/sa818/sa818_module.cpp` (implement `do_get`)
- Modify: `tests/sim_shell/pytest/test_module_iface.py`

**Interfaces:**
- Consumes: `sa818_at_read_rssi(dev, &u8)`, `sa818_get_status(dev)`, `g_shadow`.
- Produces: `get` for `rssi` (telemetry) plus current-value reads for `frequency`/`volume`/`power_level`/`bandwidth`/`ptt`; `read_only` on `set rssi`; `unknown_capability`.

- [ ] **Step 1: Write the failing tests** — append to `test_module_iface.py`

```python
def test_module_get_rssi(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module get rssi")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is True
    assert r["cap"] == "rssi" and r["op"] == "get"
    assert isinstance(r["value"], int)


def test_module_get_frequency_reads_shadow(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    shell.exec_command("module set frequency 146.000")
    out = shell.exec_command("module get frequency")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is True and r["value"] == 146.0


def test_module_set_rssi_read_only(shell):
    out = shell.exec_command("module set rssi 5")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "read_only"


def test_module_unknown_capability(shell):
    out = shell.exec_command("module set banana 1")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "unknown_capability"


def test_module_get_unknown_capability(shell):
    out = shell.exec_command("module get banana")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "unknown_capability"
```

- [ ] **Step 2: Run to verify failure** — twister command, grep `test_module_get|test_module_set_rssi_read_only|test_module_unknown`. Expected: FAIL.

- [ ] **Step 3: Implement.** In `do_set`, add a `read_only` guard for `rssi` before the final `unknown_capability` return:

```cpp
  if (!strcmp(cap, "rssi")) {
    result_err(sh, cap, "set", "read_only");
    return 0;
  }
```

Replace the stub `do_get` with:

```cpp
int do_get(const struct shell *sh, const char *cap) {
  const struct device *dev = sa818_dev();
  if (!dev || !device_is_ready(dev)) {
    result_err(sh, cap, "get", "driver_error");
    return 0;
  }

  if (!strcmp(cap, "rssi")) {
    uint8_t rssi = 0;
    if (sa818_at_read_rssi(dev, &rssi) != SA818_OK) {
      result_err(sh, cap, "get", "driver_error");
      return 0;
    }
    result_ok_int(sh, cap, "get", (int)rssi);
    return 0;
  }
  if (!strcmp(cap, "frequency")) {
    result_ok_float(sh, cap, "get", (double)g_shadow.freq);
    return 0;
  }
  if (!strcmp(cap, "bandwidth")) {
    result_ok_str(sh, cap, "get", g_shadow.bw == SA818_BW_25_KHZ ? "25" : "12.5");
    return 0;
  }

  sa818_status st = sa818_get_status(dev);
  if (!strcmp(cap, "volume")) {
    result_ok_int(sh, cap, "get", (int)st.volume);
    return 0;
  }
  if (!strcmp(cap, "power_level")) {
    result_ok_str(sh, cap, "get", st.power_level == SA818_POWER_HIGH ? "high" : "low");
    return 0;
  }
  if (!strcmp(cap, "ptt")) {
    result_ok_bool(sh, cap, "get", st.ptt_state == SA818_PTT_ON);
    return 0;
  }

  result_err(sh, cap, "get", "unknown_capability");
  return 0;
}
```

- [ ] **Step 4: Run to verify pass** — twister command; all new tests PASS, and the full `test_module_iface.py` suite is green.

- [ ] **Step 5: Format + commit**

```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation && clang-format-18 -i drivers/radio/sa818/sa818_module.cpp && \
git add drivers/radio/sa818/sa818_module.cpp tests/sim_shell/pytest/test_module_iface.py && \
git commit -m "feat(module): \`get\` telemetry/current values + read-only/unknown-cap errors

Refs #36." && git push
```

---

## Task 5: Enable CI + fix pre-existing wav-test flake; final verification

**Files:**
- Modify: `.github/workflows/ci.yml`
- Modify: `tests/sim_shell/pytest/test_sim_shell.py`

**Interfaces:** none (CI + test-harness robustness only).

- [ ] **Step 1: Reproduce the pre-existing wav failure**

Run the full suite (twister command from Task 2 Step 2, no grep). Confirm the ONLY failing test is `test_sim_shell.py::test_wav_load_start_adc`, failing because the harness cannot match the echoed long `tmp_path` (line-wrap), NOT an audio-logic error.

- [ ] **Step 2: Point the wav test at a short path** — in `tests/sim_shell/pytest/test_sim_shell.py::test_wav_load_start_adc`, replace the `tmp_path`-based path with a short fixed path so the echoed shell command does not wrap.

Change:
```python
    wav_path = tmp_path / "test.wav"
    _write_pcm16_mono_wav(str(wav_path), sr, samples)
    out = shell.exec_command(f"wav load {wav_path}")
```
to:
```python
    # Use a short path: a long tmp_path line-wraps in the shell echo and breaks
    # the harness's command match (test-harness robustness, not audio logic).
    wav_path = "/tmp/t.wav"
    _write_pcm16_mono_wav(wav_path, sr, samples)
    out = shell.exec_command(f"wav load {wav_path}")
```
(Leave the rest of the test unchanged. If the `tmp_path` fixture parameter becomes unused, keep it — removing it is optional and out of scope.)

- [ ] **Step 3: Run full suite to verify 100% green**

```bash
cd /home/pbuchegger/OE5XRX && . .zephyr-venv/bin/activate && \
  export ZEPHYR_BASE=/home/pbuchegger/OE5XRX/zephyr ZEPHYR_TOOLCHAIN_VARIANT=host && \
  cd FW-RemoteStation && west twister -T tests/sim_shell -p native_sim/native/64 -c --inline-logs 2>&1 | \
  grep -E "executed test cases passed|scenario\(s\) failed"
```
Expected: `... executed test configurations passed (100.00%)`, 0 failed.

- [ ] **Step 4: Enable the sim_shell job in CI** — in `.github/workflows/ci.yml`, uncomment the `Twister system tests (shell)` step so it reads:

```yaml
      - name: Twister system tests (shell)
        working-directory: fw
        shell: bash
        run: |
          set -euo pipefail
          west twister -T tests/sim_shell -p native_sim/native/64 -v --inline-logs
```
(Leave the `Twister unit tests` step commented — no such suite is in scope for D1.)

- [ ] **Step 5: Verify app build + clang-format parity with CI**

```bash
cd /home/pbuchegger/OE5XRX && . .zephyr-venv/bin/activate && \
  export ZEPHYR_BASE=/home/pbuchegger/OE5XRX/zephyr ZEPHYR_TOOLCHAIN_VARIANT=host && \
  cd FW-RemoteStation && west build -b native_sim/native/64 app -p always 2>&1 | tail -3
# clang-format check over CI's paths:
mapfile -t FILES < <(find app boards tests -type f \( -name "*.c" -o -name "*.h" -o -name "*.cc" -o -name "*.hh" -o -name "*.cpp" -o -name "*.hpp" \) -print)
clang-format-18 -i "${FILES[@]}" && git diff --quiet && echo "clang-format OK" || (echo "clang-format made changes"; git --no-pager diff --name-only)
```
Expected: app builds; `clang-format OK` (no diff). If clang-format changed files, review + include them in the commit.

- [ ] **Step 6: Commit**

```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation && \
git add .github/workflows/ci.yml tests/sim_shell/pytest/test_sim_shell.py && \
git commit -m "ci: run tests/sim_shell twister job; fix long-path wav test flake

Enable the sim_shell system-test job so the module describe->set->sa818 E2E gates PRs.
The pre-existing test_wav_load_start_adc failed only because a long tmp_path line-wraps
in the shell echo; point it at a short path (harness robustness, no audio change).

Refs #36." && git push
```

---

## Task 6 (optional): ztest for pure helpers

Only do this if the value-parsing / range-validation logic can be factored into pure free functions without coupling to the shell — otherwise the pytest E2E in Tasks 1–4 is authoritative and this task is skipped. If pursued: extract `parse_bool` and a `validate_frequency(float)->err_code` into a small header, add a `tests/unit_module/` ztest, and add its twister invocation to CI. Do not refactor the dispatchers if it would add indirection for its own sake (YAGNI). If skipped, note it in the PR.

---

## Review & Completion (after Task 5)

- [ ] **Round 2 — Watchers:** dispatch `atlas` (spec-compliance vs meta-spec §5.1/§5.2/§8) and `audit` (C++/Zephyr code quality) on `drivers/radio/sa818/sa818_module.cpp` + the test file. Address findings.
- [ ] **Integration (`probe`) — full E2E:** describe → `set frequency 145.500` → SA818 driver → `SA818Simulator.freq_tx == 145.5`; verify JSON validity and every error code. Confirm the `sa818` tree still works.
- [ ] **verification-before-completion:** run the full `sim_shell` twister suite + app build + clang-format, paste evidence.
- [ ] **Copilot loop:** set PR #38 "Ready for review", run the `copilot-loop` skill (Opus code-quality), address findings until approved.
- [ ] PR body: reference meta-spec §8, `Closes #36`, un-draft.

---

## Self-Review (against the spec)

- **§5.1 capability schema** → Task 1 describe (identity + 6 typed caps w/ constraints/access). ✓
- **§5.2 thin firmware** → no persistence (RAM shadow is working state), no access model, no platform knowledge. ✓
- **§8 machine-readable describe** → Task 1 `MODULE-DESCRIBE` single JSON line. ✓
- **§8 typed frequency end-to-end** → Task 2 `test_module_set_frequency_e2e` asserts `freq_tx == 145.5`. ✓
- **§8/§9 not parsing the human prompt** → token-prefixed JSON out; positional args in. ✓
- **Full capability set** (frequency/ptt/power_level/rssi/volume/bandwidth) → Tasks 2–4. ✓
- **Existing sa818 tests unbroken** → Task 1 enables SA818_SHELL; every task re-runs the full suite. ✓
- **CI green (build_and_tests + clang_format + sim_shell)** → Task 5. ✓
- **Prereq Kconfig (driver must build)** → Task 1 Step 2. ✓
