# Module Control-Protocol: Quiet Status + Atomic Framing — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the FM module's `MODULE-*` control protocol quiet (pull-based status, no unsolicited spam) and robustly framed (responses emitted atomically), and advertise an `audio` capability in `describe`.

**Architecture:** Three independent slices. (A) A Kconfig change removes the second, racing UART log backend so logs route only through the shell backend (same UART, serialized). (B) The periodic status `LOG_INF` in the app main loop is deleted. (C) The generic capability framework gains an `audio` kind + `stream` type and an aggregate `snapshot`, and the SA818 module gains an `audio` capability and a `module <id> status` verb — both system-tested via the `tests/sim_shell` pytest harness.

**Tech Stack:** Zephyr RTOS (C++20, ETL, no heap/exceptions/RTTI), Zephyr shell, `west`/Twister, pytest (`SA818Simulator` over PTY).

**Spec:** `docs/superpowers/specs/2026-09-09-module-protocol-quiet-framing-design.md`

## Global Constraints

- **No dynamic allocation, no exceptions, no RTTI.** Allowed: `std::array/span/string_view/optional/variant`, `constexpr`, fixed buffers, ETL containers. Forbidden: `new/delete/malloc`, `std::string/vector/map/function`, `std::optional<std::string>`.
- **`include/oe5xrx/module/iface.h` has NO device/RTOS dependencies** — keep it that way.
- **clang-format-18** over `app/`, `boards/`, `tests/`: run before every commit; CI `clang_format` fails on any diff. (Header lives under `include/` — still format it manually for consistency; not gated but keep clean.)
- **Driver result codes are `[[nodiscard]]`** — never discard `sa818_*` return values.
- **160-column limit**, 2-space indent (LLVM base, `.clang-format` at repo root).
- **ETL overflow panics** — size any fixed buffers correctly.
- **Firmware stays thin** — no persistence/access/platform logic.
- **Do not touch `tests/sim_shell/prj.conf`** (native_sim log routing is irrelevant to the hardware framing bug; leave the green harness alone).
- **Single feature branch, single PR** (`feat/module-protocol-quiet-framing`). Do not merge — hardware validation is required first.

## File Structure

- Modify `app/prj.conf` — drop `CONFIG_LOG_BACKEND_UART`, make `CONFIG_SHELL_LOG_BACKEND` explicit with a rationale comment (Task 1).
- Modify `app/src/main_usb_audio.cpp` — remove the periodic status `LOG_INF`; park the main thread (Task 2).
- Modify `include/oe5xrx/module/iface.h` — `Kind::Audio`, `ValueType::Stream`, `AudioInfo` mixin, `Result::renderValueOnly`, `Module::snapshot` (Tasks 3, 4).
- Modify `subsys/module/devices/sa818/sa818_module.cpp` — `AUDIO_SPEC`/`AudioCap` + registry entry (Task 3); `MODULE-STATUS` buffer + `status` verb + updated `SHELL_CMD_REGISTER` help (Task 4).
- Modify `tests/sim_shell/pytest/test_module_iface.py` — assert `audio` in `describe` (Task 3); new `MODULE-STATUS` tests (Task 4).

---

### Task 1: Framing fix — remove the racing UART log backend

**Files:**
- Modify: `app/prj.conf:64-67` (the `# Logging` block)

**Interfaces:**
- Consumes: nothing.
- Produces: a shipped `fm_board` build whose resolved `.config` has `CONFIG_LOG_BACKEND_UART` **unset** and `CONFIG_SHELL_LOG_BACKEND=y`. No code depends on this symbolically; it changes runtime framing only.

**Context:** `CONFIG_SHELL_BACKEND_SERIAL=y` already turns on `CONFIG_SHELL_LOG_BACKEND=y` (default y). Today `CONFIG_LOG_BACKEND_UART=y` adds a *second*, async writer to the same CDC-ACM UART (flushed by `LOG_PROCESS_THREAD`, `CONFIG_LOG_MODE_DEFERRED=y`), which rips `shell_print` lines. Removing it keeps logs on the UART via the shell backend, serialized with shell output.

- [ ] **Step 1: Edit the Logging block**

Replace the current block:

```
# =============================================================================
# Logging
# =============================================================================
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=3
CONFIG_LOG_BACKEND_UART=y
```

with:

```
# =============================================================================
# Logging
# =============================================================================
# Logs deliberately stay on the CDC-ACM control UART, but MUST route ONLY
# through the shell log backend -- NOT a second, direct UART backend.
#
# CONFIG_SHELL_BACKEND_SERIAL enables CONFIG_SHELL_LOG_BACKEND (default y): the
# shell thread serializes log flushing with its own shell_print output, so a
# MODULE-* protocol response line is emitted whole. A direct CONFIG_LOG_BACKEND_UART
# is a SECOND, async writer (LOG_PROCESS_THREAD, deferred mode) to the same UART
# with no mutual exclusion vs. the shell -- it splits MODULE-* lines mid-string and
# makes the station-agent discovery handshake flap OFFLINE. So it stays OFF.
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=3
CONFIG_SHELL_LOG_BACKEND=y
# (CONFIG_LOG_BACKEND_UART intentionally NOT set -- see comment above.)
```

- [ ] **Step 2: Build the shipped app and verify resolved config**

Run:
```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation
west build -b fm_board -d build-fm app 2>&1 | tail -5
grep -E 'CONFIG_(LOG_BACKEND_UART|SHELL_LOG_BACKEND)=' build-fm/zephyr/.config
```
Expected: build succeeds; the grep shows `CONFIG_SHELL_LOG_BACKEND=y` and **no** `CONFIG_LOG_BACKEND_UART=y` line. (If `west build -b fm_board` is unavailable in this environment, build `native_sim`: `west build -b native_sim/native/64 -d build-ns app` and grep `build-ns/zephyr/.config` — the same assertion holds.)

- [ ] **Step 3: Commit**

```bash
git add app/prj.conf
git commit -m "fix(module): route logs only via shell backend so MODULE-* frames stay atomic"
```

---

### Task 2: Remove the periodic unsolicited status line

**Files:**
- Modify: `app/src/main_usb_audio.cpp:146-153`

**Interfaces:**
- Consumes: nothing.
- Produces: an app main loop that no longer emits `SA818 Status - Power…` and no longer calls `sa818_get_status` on a timer.

**Context:** The 10 s loop's only job was the status `LOG_INF`. `main` must not return (returning ends the thread; harmless but pointless), so park the thread instead. Boot-confirm and the shell run on their own threads and are unaffected.

- [ ] **Step 1: Replace the main-loop body**

Replace:

```cpp
  while (true) {
    k_sleep(K_SECONDS(10));

    /* Optional: Print status periodically */
    sa818_status status = sa818_get_status(sa818);
    LOG_INF("SA818 Status - Power: %s, PTT: %s, SQL: %s", status.device_power == SA818_DEVICE_ON ? "ON" : "OFF",
            status.ptt_state == SA818_PTT_ON ? "ON" : "OFF", status.squelch_state == SA818_SQUELCH_OPEN ? "OPEN" : "CLOSED");
  }

  return 0;
```

with:

```cpp
  /* No unsolicited status: the control UART stays quiet. Live state is pulled on
   * demand via `module fm status`. The shell + boot-confirm run on their own
   * threads; the main thread has nothing left to do, so it parks. */
  k_sleep(K_FOREVER);

  return 0;
```

- [ ] **Step 2: Build and verify the string is gone**

Run:
```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation
west build -b fm_board -d build-fm app 2>&1 | tail -5
grep -rn "Status - Power" app/src/ || echo "OK: periodic status line removed"
```
Expected: build succeeds; grep prints the `OK:` line (no match). If `sa818` is now unused in the resulting scope, confirm there is no `-Wunused` error in the build output (it is still used earlier for `sa818_set_power` / `boot_confirm_fm_start`, so no warning is expected).

- [ ] **Step 3: Commit**

```bash
git add app/src/main_usb_audio.cpp
git commit -m "fix(module): drop periodic unsolicited SA818 status line (pull via status)"
```

---

### Task 3: `audio` capability in `describe`

**Files:**
- Modify: `include/oe5xrx/module/iface.h` (enums + `AudioInfo` mixin)
- Modify: `subsys/module/devices/sa818/sa818_module.cpp` (spec + cap + registry)
- Test: `tests/sim_shell/pytest/test_module_iface.py:25-63` (`test_module_describe_valid_json`)

**Interfaces:**
- Consumes: existing `mod::Capability`, `mod::FieldSpec`, `mod::ValueType`, `mod::Kind`, `mod::Result`, `mod::Op`.
- Produces:
  - `mod::Kind::Audio` (renders `"audio"`), `mod::ValueType::Stream` (renders `"stream"`).
  - `class mod::AudioInfo : public mod::Capability` — get-only; `handle(Op::Get,…)→onGet()`, else `Result::err("wrong_op")`.
  - `AudioCap` in the SA818 TU, registered in `g_caps`, with `spec()→AUDIO_SPEC` (`{"audio", ValueType::Stream}`) and `onGet()→Result::okStr("uac2")`.

- [ ] **Step 1: Write the failing test** — extend `test_module_describe_valid_json`

In `tests/sim_shell/pytest/test_module_iface.py`, change the capability-set assertion (line ~36) to include `audio`, and add shape assertions. Replace:

```python
    caps = {c["name"]: c for c in d["capabilities"]}
    assert set(caps) == {"frequency", "tx_frequency", "rx_frequency", "ptt", "power_level", "rssi", "volume", "bandwidth", "squelch", "tx_tone", "rx_tone", "band"}
```

with:

```python
    caps = {c["name"]: c for c in d["capabilities"]}
    assert set(caps) == {"frequency", "tx_frequency", "rx_frequency", "ptt", "power_level", "rssi", "volume", "bandwidth", "squelch", "tx_tone", "rx_tone", "band", "audio"}

    # audio path is declared as a capability so the agent derives it from the schema
    assert caps["audio"]["kind"] == "audio"
    assert caps["audio"]["type"] == "stream"
    assert caps["audio"]["access"] == "operator"
    assert "unit" not in caps["audio"]
    assert "ranges" not in caps["audio"]
    assert "values" not in caps["audio"]
    assert "readonly" not in caps["audio"]
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation
west twister -T tests/sim_shell -p native_sim/native/64 -v --clobber-output -s tests/sim_shell/tests.sim_shell 2>&1 | tail -30
```
Expected: `test_module_describe_valid_json` FAILS — `audio` not in the capability set (KeyError / set mismatch). (If the `-s` scenario name differs, run without `-s`; the whole sim_shell suite runs and the describe test fails.)

- [ ] **Step 3: Extend the framework header**

In `include/oe5xrx/module/iface.h`:

Add `Audio` to the `Kind` enum:
```cpp
enum class Kind { Setting, Action, Telemetry, Audio };
```
Add the `kindStr` arm (inside the `switch`, before the closing `}`):
```cpp
  case Kind::Audio:
    return "audio";
```
Add `Stream` to the `ValueType` enum:
```cpp
enum class ValueType { Bool, Int, Float, Enum, String, Stream };
```
Add the `typeStr` arm:
```cpp
  case ValueType::Stream:
    return "stream";
```
Add the `AudioInfo` mixin next to the other mixins (after the `Telemetry` class, before `struct Identity`):
```cpp
/** @brief Kind mixin: an audio stream endpoint. Descriptor-only; `get`->onGet
 *  (a declarative transport id), `set`/`do`->wrong_op. The agent keys off the
 *  advertised `kind:"audio"` capability to derive that an audio path exists. */
class AudioInfo : public Capability {
public:
  Kind kind() const override { return Kind::Audio; }
  Result handle(Op op, const char *) override {
    switch (op) {
    case Op::Get:
      return onGet();
    case Op::Set:
    case Op::Do:
    default:
      return Result::err("wrong_op");
    }
  }
};
```

Note: `ValueType::Stream` has no numeric range/enum, so the generic `describe` renderer emits only `name/kind/type/access` for it (its `numStr` path is never reached because `AUDIO_SPEC` has no `ranges`/`enumValues`). No change to `describe()` or `numStr` is needed.

- [ ] **Step 4: Add the `audio` capability to the SA818 module**

In `subsys/module/devices/sa818/sa818_module.cpp`:

Add `using mod::AudioInfo;` to the `using` block (after `using mod::Action;`).

Add the field spec next to the other `*_SPEC` definitions (after `BAND_SPEC`, ~line 159):
```cpp
const FieldSpec AUDIO_SPEC{"audio", ValueType::Stream};
```

Add the capability class after `BandCap` (before the `tone_is_clear` helper, ~line 449):
```cpp
/* Declarative audio-path capability: the SA818 FM module streams RX/TX audio over the
 * UAC2 USB interface. This carries no scalar value and no driver call -- it exists so the
 * agent can derive "audio path present" from the capability schema (single source of
 * truth). `get` returns the transport identifier; it is side-effect free. */
class AudioCap : public AudioInfo {
public:
  const FieldSpec &spec() const override { return AUDIO_SPEC; }

protected:
  Result onGet() override { return Result::okStr("uac2"); }
};
```

Add the instance next to the other `g_*` cap instances (after `BandCap g_band{g_ctx};`, ~line 578):
```cpp
AudioCap g_audio;
```

Append it to the registry array (`g_caps`, ~line 580) as the last entry:
```cpp
Capability *const g_caps[] = {&g_freq,   &g_txfreq,   &g_rxfreq, &g_ptt,     &g_power, &g_rssi,
                              &g_volume, &g_bandwidth, &g_squelch, &g_txtone, &g_rxtone, &g_band, &g_audio};
```

- [ ] **Step 5: Run the test to verify it passes**

Run:
```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation
west twister -T tests/sim_shell -p native_sim/native/64 -v --clobber-output 2>&1 | tail -30
```
Expected: `test_module_describe_valid_json` PASSES and the rest of the sim_shell suite stays green.

- [ ] **Step 6: Format**

Run:
```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation
clang-format-18 -i subsys/module/devices/sa818/sa818_module.cpp include/oe5xrx/module/iface.h
git diff --stat
```
Expected: files conform (re-inspect the `g_caps` array — clang-format will re-wrap it; that is fine).

- [ ] **Step 7: Commit**

```bash
git add include/oe5xrx/module/iface.h subsys/module/devices/sa818/sa818_module.cpp tests/sim_shell/pytest/test_module_iface.py
git commit -m "feat(module): advertise audio capability (kind=audio,type=stream) in describe"
```

---

### Task 4: `module <id> status` pull query (`MODULE-STATUS`)

**Files:**
- Modify: `include/oe5xrx/module/iface.h` (`Result::renderValueOnly`, `Module::snapshot`)
- Modify: `subsys/module/devices/sa818/sa818_module.cpp` (`STATUS_BUF_SIZE`, `status` verb, help text)
- Test: `tests/sim_shell/pytest/test_module_iface.py` (new tests)

**Interfaces:**
- Consumes: `mod::Module`, `mod::Capability::handle`, `mod::Op::Get`, `mod::JsonWriter`, `mod::Result` from Tasks 3 and earlier.
- Produces:
  - `void mod::Result::renderValueOnly(JsonWriter&) const` — public; emits the value arm, or `null` when the result is an error.
  - `void mod::Module::snapshot(JsonWriter&) const` — emits `{"schema":1,"module":<id>,"values":{<cap>:<value>,…}}`, calling `handle(Op::Get,"")` per cap.
  - Shell verb `module <id> status` → line `MODULE-STATUS {…}`; unknown module → `MODULE-RESULT {…,"op":"status","error":"unknown_module"}`; overflow → `MODULE-STATUS {"schema":1,"module":"<id>","error":"too_long"}`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/sim_shell/pytest/test_module_iface.py`:

```python
def test_module_status_snapshot(sa818_sim, shell):
    """`module fm status` returns one MODULE-STATUS line snapshotting every capability."""
    shell.exec_command("sa818 power on")
    out = shell.exec_command("module fm status")
    d = _payload(out, "MODULE-STATUS")
    assert d["schema"] == 1
    assert d["module"] == "fm"
    v = d["values"]
    # every advertised capability appears in the snapshot
    assert set(v) == {"frequency", "tx_frequency", "rx_frequency", "ptt", "power_level",
                      "rssi", "volume", "bandwidth", "squelch", "tx_tone", "rx_tone",
                      "band", "audio"}
    # types match the describe schema
    assert isinstance(v["frequency"], float)
    assert isinstance(v["ptt"], bool)
    assert isinstance(v["volume"], int)
    assert v["power_level"] in ("low", "high")
    assert v["audio"] == "uac2"


def test_module_status_reflects_live_state(sa818_sim, shell):
    shell.exec_command("sa818 power on")
    shell.exec_command("module fm set frequency 146.000")
    shell.exec_command("module fm do ptt on")
    d = _payload(shell.exec_command("module fm status"), "MODULE-STATUS")
    assert d["values"]["frequency"] == 146.0
    assert d["values"]["ptt"] is True


def test_module_status_unknown_module(shell):
    out = shell.exec_command("module nope status")
    r = _payload(out, "MODULE-RESULT")
    assert r["ok"] is False and r["error"] == "unknown_module"
    assert r["op"] == "status"
```

- [ ] **Step 2: Run the tests to verify they fail**

Run:
```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation
west twister -T tests/sim_shell -p native_sim/native/64 -v --clobber-output 2>&1 | tail -30
```
Expected: the three new tests FAIL — `module fm status` currently returns a `MODULE-RESULT {…,"error":"usage"}` line (unrecognized op), so `_payload(out, "MODULE-STATUS")` raises "no MODULE-STATUS line".

- [ ] **Step 3: Add the value/snapshot renderers to the header**

In `include/oe5xrx/module/iface.h`, add a **public** method to `Result` (right after the `render(...)` method, before `private:`):

```cpp
  /** Render just the value arm as JSON (integer/float/bool/quoted string), or `null`
   *  when this result is an error. Used by Module::snapshot for the aggregate status. */
  void renderValueOnly(JsonWriter &w) const {
    if (!ok_) {
      w.raw("null");
      return;
    }
    renderValue(w);
  }
```

Add `snapshot` to `Module` (after the existing `describe(...)` method):

```cpp
  /** Render `{"schema":1,"module":<id>,"values":{<cap>:<value>,…}}` — a live snapshot
   *  of every capability's `get` value. A cap whose get errors renders as `null`, so one
   *  unavailable cap never fails the whole snapshot. */
  void snapshot(JsonWriter &w) const {
    w.ch('{');
    w.kvRaw("schema", "1");
    w.ch(',');
    w.kvStr("module", moduleId_);
    w.ch(',');
    w.key("values");
    w.ch('{');
    bool first = true;
    for (Capability *c : caps_) {
      if (!first) {
        w.ch(',');
      }
      first = false;
      w.key(c->name());
      Result r = c->handle(Op::Get, "");
      r.renderValueOnly(w);
    }
    w.ch('}');
    w.ch('}');
  }
```

Note: `handle` is non-const, but `caps_` is `span<Capability *const>` (const pointer, mutable pointee), so calling `handle` from a `const` `Module` method is valid — this mirrors the existing `execute(...) const`.

- [ ] **Step 4: Add the `status` verb to the shell command**

In `subsys/module/devices/sa818/sa818_module.cpp`:

Add the buffer-size constant next to the others (after `DESCRIBE_BUF_SIZE`, ~line 135):
```cpp
constexpr size_t STATUS_BUF_SIZE = 768;
```

In `cmd_module`, add the `status` handler **immediately after** the `describe` block (after its closing `}` near line 640, before the `set` block):
```cpp
  if (!strcmp(op, "status")) {
    if (m == nullptr) {
      emit_result(sh, Result::err("unknown_module"), id, "", "status");
      return 0;
    }
    static char buf[STATUS_BUF_SIZE]; // static: single-threaded shell, keep off the 2K stack (see note at RESULT_BUF_SIZE)
    mod::JsonWriter w(buf, sizeof(buf));
    w.raw("MODULE-STATUS ");
    m->snapshot(w);
    if (w.truncated()) {
      // Snapshot outgrew the buffer: emit a minimal valid frame rather than truncated
      // (invalid) JSON. moduleId is a registered literal, so no escaping is needed.
      shell_print(sh, "MODULE-STATUS {\"schema\":1,\"module\":\"%s\",\"error\":\"too_long\"}", m->moduleId());
      return 0;
    }
    shell_print(sh, "%s", w.c_str());
    return 0;
  }
```

Update the shell help string in `SHELL_CMD_REGISTER` (~line 684) to include `status`:
```cpp
SHELL_CMD_REGISTER(module, NULL, "module list | module <id> describe|status|set|get|do <cap> [value]", cmd_module);
```

- [ ] **Step 5: Run the tests to verify they pass**

Run:
```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation
west twister -T tests/sim_shell -p native_sim/native/64 -v --clobber-output 2>&1 | tail -30
```
Expected: the three new tests PASS and the full sim_shell suite stays green.

- [ ] **Step 6: Format**

Run:
```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation
clang-format-18 -i subsys/module/devices/sa818/sa818_module.cpp include/oe5xrx/module/iface.h
git diff --stat
```
Expected: no further reformatting needed (or trivial re-wraps).

- [ ] **Step 7: Commit**

```bash
git add include/oe5xrx/module/iface.h subsys/module/devices/sa818/sa818_module.cpp tests/sim_shell/pytest/test_module_iface.py
git commit -m "feat(module): add pull-based 'module <id> status' snapshot (MODULE-STATUS frame)"
```

---

### Task 5: Full local CI gate + PR

**Files:** none (verification + PR only).

**Interfaces:**
- Consumes: all prior tasks.
- Produces: green local test run + an open PR (NOT merged).

- [ ] **Step 1: Run the full gated test set**

Run:
```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation
west twister -T tests/sim_shell -p native_sim/native/64 -v --clobber-output 2>&1 | tail -20
west twister -T tests/etl -p native_sim/native/64 -v --clobber-output 2>&1 | tail -10
west twister -T app --integration -v --clobber-output 2>&1 | tail -20
```
Expected: all green. (`tests/boot_confirm`, `tests/usb_audio`, `tests/unit_audio` are also CI-gated but are untouched by this change; run them too if the environment builds them.)

- [ ] **Step 2: clang-format gate (mirror CI)**

Run:
```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation
clang-format-18 --dry-run --Werror $(git diff --name-only main... -- 'app/**' 'boards/**' 'tests/**' | grep -E '\.(c|h|cc|hh|cpp|hpp)$') && echo "clang-format OK"
```
Expected: `clang-format OK` (no diff on the CI-scoped dirs). The header under `include/` is outside the CI scope but was formatted in Tasks 3–4.

- [ ] **Step 3: Push and open the PR (do NOT merge)**

```bash
cd /home/pbuchegger/OE5XRX/FW-RemoteStation
git push -u origin feat/module-protocol-quiet-framing
```
Open a PR titled `feat(module): quiet pull-based status + atomic framing + audio capability` whose body summarizes the three slices, links the spec, and states clearly: **needs hardware flash + validation (§7 of the spec) before merge.** Leave it open.

---

## Self-Review

**Spec coverage:**
- §1.1 remove periodic status → Task 2. ✅
- §1.2 audio capability in describe → Task 3. ✅
- §1.3 atomic framing → Task 1. ✅
- §4.1 `module <id> status` / `MODULE-STATUS` (content, truncation, unknown-module) → Task 4. ✅
- §4.2 `audio` cap shape + `get`→"uac2" + set/do→wrong_op → Task 3 (AudioInfo mixin gates set/do to wrong_op; onGet→okStr("uac2")). ✅
- §5 framework extension (Kind::Audio, ValueType::Stream, AudioInfo, renderValueOnly, snapshot) → Tasks 3 & 4. ✅
- §6 testing (describe includes audio; status snapshot; live-state; existing green) → Tasks 3, 4, 5. ✅
- §7 hardware validation handoff → Task 5 Step 3 (PR left open). ✅

**Type consistency:** `Kind::Audio`, `ValueType::Stream`, `AudioInfo`, `AudioCap`, `AUDIO_SPEC`, `renderValueOnly`, `snapshot`, `STATUS_BUF_SIZE`, `MODULE-STATUS` used identically across Tasks 3–4. `g_caps` array updated once (Task 3) and consumed by `snapshot` (Task 4). ✅

**Placeholder scan:** every code step contains concrete code; every test step has runnable commands and explicit expected outcomes. ✅
