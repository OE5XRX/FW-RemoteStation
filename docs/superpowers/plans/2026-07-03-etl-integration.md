# ETL Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Embedded Template Library (ETL) available and correctly configured in the FW-RemoteStation Zephyr build (app + tests + `fm_board`), without rewriting any existing code.

**Architecture:** ETL ships its own Zephyr module (`etl/zephyr/{module.yml,CMakeLists.txt,Kconfig}`). We pin it as a west project, enable it via `CONFIG_ETL=y`, route its runtime errors to a fail-fast Zephyr handler (`printk` + `k_panic`) registered through `SYS_INIT` at `PRE_KERNEL_1`, and prove usability with a dedicated `native_sim` Ztest.

**Tech Stack:** Zephyr (west manifest, Kconfig, CMake), C++20, ETL 20.48.0, Ztest, twister, clang-format-18.

## Global Constraints

- No dynamic allocation; no exceptions; no RTTI (do **not** enable `CONFIG_CPP_EXCEPTIONS` / `CONFIG_CPP_RTTI`). — verbatim repo rule.
- ETL pinned to tag **`20.48.0`** (no floating/AUTOREV).
- `build_and_tests` **and** `clang_format` CI jobs must stay green; new C++ must pass clang-format-18.
- Do **not** modify `CLAUDE.md` / `CONTRIBUTING.md` (separate session).
- Do **not** modify existing driver/app logic (SA818 refactor #40 is separate).
- All work on branch `feature/etl-integration` in worktree `/home/pbuchegger/OE5XRX/FW-RemoteStation-etl`.

## Controller pre-step (west repoint — done once, restored in Task 5)

The shared west manifest (`/home/pbuchegger/OE5XRX/.west/config`) points at the main tree
(where the D1 session lives). For local verification, the controller repoints it at this
worktree for the duration of implementation and restores it in Task 5:

```bash
# Save current value (expect: FW-RemoteStation)
west config manifest.path            # note the output
west config manifest.path FW-RemoteStation-etl
```

`west update` (Task 1) then fetches ETL. ETL is additive and D1 does not set `CONFIG_ETL`,
so D1 builds are functionally unaffected. Task 5 restores `manifest.path` to
`FW-RemoteStation`.

## File Structure

- `west.yml` (modify) — add `etlcpp` remote + pinned `etl` project.
- `app/prj.conf` (modify) — `CONFIG_ETL=y`, `CONFIG_ETL_LOG_ERRORS=y`.
- `app/src/etl_error_handler.cpp` (create) — SYS_INIT-registered error handler.
- `app/CMakeLists.txt` (modify) — compile the handler TU.
- `tests/etl/{CMakeLists.txt,prj.conf,testcase.yaml,src/main.cpp}` (create) — proof Ztest.
- `.github/workflows/ci.yml` (modify) — add twister step for `tests/etl`.
- `tests/sim_shell/prj.conf` (modify) — `CONFIG_ETL=y`, `CONFIG_ETL_LOG_ERRORS=y`.
- `tests/sim_shell/CMakeLists.txt` (modify) — compile the handler TU (one handler per build).

---

### Task 1: Pin ETL as a west project

**Files:**
- Modify: `west.yml`

**Interfaces:**
- Produces: a west project `etl` at workspace path `modules/lib/etl`, pinned to tag `20.48.0`, exposing Kconfig `CONFIG_ETL` and CMake target `etl::etl`.

- [ ] **Step 1: Edit `west.yml`** — add the `etlcpp` remote and the `etl` project. Full file after edit:

```yaml
manifest:
  self:
    west-commands: scripts/west-commands.yml

  remotes:
    - name: zephyrproject-rtos
      url-base: https://github.com/zephyrproject-rtos
    - name: etlcpp
      url-base: https://github.com/ETLCPP

  projects:
    - name: zephyr
      remote: zephyrproject-rtos
      revision: main
      import:
        name-allowlist:
          - cmsis_6
          - hal_stm32
    - name: etl
      remote: etlcpp
      revision: 20.48.0
      path: modules/lib/etl
```

- [ ] **Step 2: Fetch it**

Run: `west update etl`
Expected: clones `ETLCPP/etl` at tag `20.48.0` into `<workspace>/modules/lib/etl`.

- [ ] **Step 3: Verify west + Zephyr see the module**

Run:
```bash
west list etl
test -f ../modules/lib/etl/zephyr/module.yml && echo "module.yml present"
```
Expected: `west list etl` prints the project at revision `20.48.0`; `module.yml present`.

- [ ] **Step 4: Commit**

```bash
git add west.yml
git commit -m "build(etl): pin ETL 20.48.0 as west project (#43)"
```

---

### Task 2: Enable ETL in the app + fail-fast error handler

**Files:**
- Modify: `app/prj.conf`
- Create: `app/src/etl_error_handler.cpp`
- Modify: `app/CMakeLists.txt`

**Interfaces:**
- Consumes: `CONFIG_ETL` / `etl::etl` from Task 1.
- Produces: a translation unit compiled into every ETL-enabled build that registers, via
  `SYS_INIT` at `PRE_KERNEL_1`, an `etl::error_handler` callback
  `void etl_error(const etl::exception&)` which reports (`printk`) and halts (`k_panic`).
  No public header — registration is automatic.

- [ ] **Step 1: Add Kconfig to `app/prj.conf`** — append under a new section:

```
# =============================================================================
# Embedded Template Library (ETL)
# =============================================================================
# No-alloc fixed-capacity containers (etl::string/vector/map...). ETL routes
# runtime errors to a registered handler (see app/src/etl_error_handler.cpp)
# instead of throwing (exceptions stay disabled).
CONFIG_ETL=y
CONFIG_ETL_LOG_ERRORS=y
```

- [ ] **Step 2: Create `app/src/etl_error_handler.cpp`**

```cpp
/*
 * Copyright (c) 2025 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Routes ETL runtime errors to Zephyr. ETL is built with exceptions disabled
 * and CONFIG_ETL_LOG_ERRORS=y, so on a failed check ETL invokes the registered
 * error handler instead of throwing. We fail fast: report the error, then panic.
 * Registered automatically via SYS_INIT at PRE_KERNEL_1 (priority 0) so the
 * fail-fast callback is armed before essentially all other init - an ETL check
 * failing during early boot must panic, not silently continue. set_callback only
 * writes a static pointer and touches no kernel services, so it is safe this early.
 *
 * The callback uses printk rather than LOG_ERR: it can run at any boot stage
 * (including before the logging subsystem is up), and printk is synchronous and
 * early-boot-safe, so the diagnostic is emitted before k_panic() halts.
 */

#include <etl/error_handler.h>
#include <etl/exception.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

namespace
{

void etl_error(const etl::exception &e)
{
	printk("ETL error: %s @ %s:%d\n", e.what(), e.file_name(), e.line_number());
	k_panic();
}

int etl_register_error_handler(void)
{
	etl::error_handler::set_callback<etl_error>();
	return 0;
}

} // namespace

SYS_INIT(etl_register_error_handler, PRE_KERNEL_1, 0);
```

- [ ] **Step 3: Compile the TU — edit `app/CMakeLists.txt`.** Change the top `target_sources` block to:

```cmake
target_sources(app PRIVATE src/main.cpp)

target_sources(app PRIVATE src/etl_error_handler.cpp)
```

- [ ] **Step 4: Build for native_sim**

Run: `west build -b native_sim/native/64 app -p always`
Expected: build succeeds; `app/src/etl_error_handler.cpp` compiled (ETL headers resolve, link OK).

- [ ] **Step 5: Build for fm_board (arm target — flushes the `<new>`/placement-new risk)**

Run: `west build -b fm_board app -p always`
Expected: build succeeds.

- [ ] **Step 6: Commit**

```bash
git add app/prj.conf app/src/etl_error_handler.cpp app/CMakeLists.txt
git commit -m "feat(etl): enable ETL in app + fail-fast Zephyr error handler (#43)"
```

---

### Task 3: Proof Ztest (`tests/etl`) + CI step

**Files:**
- Create: `tests/etl/src/main.cpp`
- Create: `tests/etl/prj.conf`
- Create: `tests/etl/CMakeLists.txt`
- Create: `tests/etl/testcase.yaml`
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: `CONFIG_ETL` / `etl::etl` (Task 1), `app/src/etl_error_handler.cpp` (Task 2).
- Produces: twister suite `fm.etl.smoke` on `native_sim/native/64`.

- [ ] **Step 1: Write the failing test — `tests/etl/src/main.cpp`**

```cpp
/*
 * Copyright (c) 2025 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Proof that a real ETL type links and works on native_sim.
 */

#include <zephyr/ztest.h>

#include <etl/string.h>

ZTEST_SUITE(etl_smoke, NULL, NULL, NULL, NULL, NULL);

ZTEST(etl_smoke, test_string_append_compare)
{
	etl::string<32> s("OE5");
	s.append("XRX");

	zassert_equal(s.size(), 6U, "unexpected size %u", (unsigned int)s.size());
	zassert_true(s == "OE5XRX", "content mismatch");
	zassert_equal(s.capacity(), 32U, "unexpected capacity %u", (unsigned int)s.capacity());
	zassert_false(s.full(), "string should not be full");
}
```

- [ ] **Step 2: Create `tests/etl/prj.conf`**

```
# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

CONFIG_ZTEST=y

CONFIG_CPP=y
CONFIG_STD_CPP20=y
CONFIG_EXTERNAL_LIBCPP=y

CONFIG_LOG=y

CONFIG_ETL=y
CONFIG_ETL_LOG_ERRORS=y
```

- [ ] **Step 3: Create `tests/etl/CMakeLists.txt`**

```cmake
# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(etl_smoke)

target_sources(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src/main.cpp
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/etl_error_handler.cpp
)
```

- [ ] **Step 4: Create `tests/etl/testcase.yaml`**

```yaml
# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

tests:
  fm.etl.smoke:
    platform_allow:
      - native_sim/native/64
    tags: etl
    harness: ztest
```

- [ ] **Step 5: Run the suite**

Run: `west twister -T tests/etl -p native_sim/native/64 -v --inline-logs`
Expected: `fm.etl.smoke` PASSES (1 testcase, 0 failures).

- [ ] **Step 6: Add the CI step — edit `.github/workflows/ci.yml`.** After the "Twister system tests (shell)" step in `build_and_tests`, insert:

```yaml
      - name: Twister ETL proof (tests/etl)
        working-directory: fw
        shell: bash
        run: |
          set -euo pipefail
          west twister -T tests/etl -p native_sim/native/64 -v --inline-logs
```

- [ ] **Step 7: Commit**

```bash
git add tests/etl .github/workflows/ci.yml
git commit -m "test(etl): native_sim proof Ztest for etl::string + CI step (#43)"
```

---

### Task 4: Make ETL available in the sim_shell test harness

**Files:**
- Modify: `tests/sim_shell/prj.conf`
- Modify: `tests/sim_shell/CMakeLists.txt`

**Interfaces:**
- Consumes: `CONFIG_ETL` (Task 1), `app/src/etl_error_handler.cpp` (Task 2).
- Produces: the existing sim_shell suite links ETL and carries exactly one error handler.

- [ ] **Step 1: Enable ETL in `tests/sim_shell/prj.conf`** — append:

```
# =============================================================================
# Embedded Template Library (ETL)
# =============================================================================
CONFIG_ETL=y
CONFIG_ETL_LOG_ERRORS=y
```

- [ ] **Step 2: Compile the handler TU — edit `tests/sim_shell/CMakeLists.txt`.** Add to the existing `target_sources(app PRIVATE ...)` list, alongside `main.cpp`:

```cmake
  ${CMAKE_CURRENT_LIST_DIR}/../../app/src/etl_error_handler.cpp
```

- [ ] **Step 3: Run the sim_shell suite (regression check)**

Run: `west twister -T tests/sim_shell -p native_sim/native/64 -v --inline-logs`
Expected: all existing sim_shell testcases still PASS (no regressions).

- [ ] **Step 4: Commit**

```bash
git add tests/sim_shell/prj.conf tests/sim_shell/CMakeLists.txt
git commit -m "test(etl): link ETL into sim_shell harness (#43)"
```

---

### Task 5: Full verification + restore shared west state

**Files:** none (verification only).

- [ ] **Step 1: clang-format check over changed C/C++**

Run:
```bash
clang-format-18 -i app/src/etl_error_handler.cpp tests/etl/src/main.cpp
git diff --quiet || (echo "clang-format changed files"; git --no-pager diff --name-only; exit 1)
```
Expected: no diff (files already formatted). If a diff appears, commit the formatting.

- [ ] **Step 2: Clean rebuild — native_sim app**

Run: `west build -b native_sim/native/64 app -p always`
Expected: PASS.

- [ ] **Step 3: Clean rebuild — fm_board app**

Run: `west build -b fm_board app -p always`
Expected: PASS.

- [ ] **Step 4: Full twister sweep (proof + existing suites)**

Run:
```bash
west twister -T tests/etl -p native_sim/native/64 -v --inline-logs
west twister -T tests/sim_shell -p native_sim/native/64 -v --inline-logs
west twister -T app --integration -v --inline-logs
```
Expected: all PASS, no regressions.

- [ ] **Step 5: Restore shared west manifest path**

Run: `west config manifest.path FW-RemoteStation`
Expected: shared `.west/config` points back at the main tree (D1 undisturbed).

- [ ] **Step 6: Confirm branch state**

Run: `git log --oneline origin/main..HEAD`
Expected: spec commit + Tasks 1–4 commits, all on `feature/etl-integration`.

---

## Notes for the executor

- ETL error API: `etl::exception::what()`, `::file_name()` → `const char*`; `::line_number()` → `int`.
- `etl::error_handler::set_callback<Fn>()` takes a compile-time free-function pointer; the anonymous-namespace function is fine as the template argument in C++17.
- ETL is header-only (`etl::etl` is an INTERFACE target); enabling `CONFIG_ETL` adds include dirs + the error-define plumbing — there is nothing to "link" beyond includes.
- If the `fm_board` build fails on `#include <new>` / placement-new (known Zephyr+ETL friction), that is a real finding — surface it; the likely fix is a Kconfig/libc tweak, not silently disabling ETL.
