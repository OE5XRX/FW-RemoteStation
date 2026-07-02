# ETL Integration — Design Spec

**Ticket:** FW-RemoteStation #43 "ETL-Integration" (Board-Item, Phase Urlaub)
**Branch:** `feature/etl-integration` (off `origin/main`)
**Date:** 2026-07-03

## Goal

Make the Embedded Template Library (ETL, `github.com/ETLCPP/etl`) available in the Zephyr
build — `etl::string<N>`, `etl::vector<T,N>`, `etl::map<...>`, … — as no-alloc,
fixed-capacity replacements for the heap-based `std::` types the repo forbids. This is
**build integration only**: ETL is made available and correctly configured. No existing
code is rewritten to use it.

## Constraints (from CLAUDE.md / CONTRIBUTING.md — binding)

- No dynamic allocation (`new`/`delete`, `malloc`/`free`, `std::vector`, `std::string`, …).
- No exceptions, no RTTI. Error handling via return/status types.
- Primarily modern C++ (C++17/20); Zephyr-C idiom at the driver/HW boundary.
- `build_and_tests` **and** `clang_format` CI jobs must stay green.

> Note: the docs do **not** yet mention ETL. A separate session updates CLAUDE.md /
> CONTRIBUTING.md wording. This work does **not** touch those files.

## Key finding — ETL is already a first-class Zephyr module

Upstream `etl/zephyr/` ships `module.yml` + `CMakeLists.txt` + `Kconfig`. So no fork and no
hand-rolled module wrapper is needed. Adding ETL as a pinned west project makes
`west update` fetch it and Zephyr auto-registers it as a module.

ETL's `zephyr/CMakeLists.txt` (verbatim, upstream):

```cmake
if(CONFIG_ETL)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/.. etl)
  zephyr_link_libraries(etl::etl)
  zephyr_compile_definitions_ifdef(CONFIG_ETL_DEBUG ETL_DEBUG)
  zephyr_compile_definitions_ifdef(CONFIG_ETL_CHECK_PUSH_POP ETL_CHECK_PUSH_POP)
  zephyr_compile_definitions_ifdef(CONFIG_ETL_LOG_ERRORS ETL_LOG_ERRORS)
endif()
```

Exposed Kconfig symbols: `CONFIG_ETL` (depends on `CPP`), `CONFIG_ETL_DEBUG`,
`CONFIG_ETL_CHECK_PUSH_POP`, `CONFIG_ETL_LOG_ERRORS`.

Latest stable release: **20.48.0** (2026-06-30).

## Decisions

| Decision | Choice |
|----------|--------|
| Pin style | Release **tag `20.48.0`** in `west.yml` (readable, immutable for ETL releases; matches repo "newest stable" rule). |
| Error routing | `CONFIG_ETL_LOG_ERRORS=y` + a registered `etl::error_handler` callback → `LOG_ERR` then `k_panic()` (fail-fast, no silent continue). |
| Profile header | **None.** Rely on ETL compiler auto-detection (GCC/Clang, C++20, no-exceptions, no-RTTI) + Kconfig. Add `etl_profile.h` only if a concrete need surfaces. |
| Proof test | Dedicated `tests/etl/` **Ztest** suite on `native_sim` + one new CI twister step. |

## Components

### 1. West manifest pin (`west.yml`)

Add an `etlcpp` remote and an `etl` project pinned to tag `20.48.0`, placed under
`modules/lib/etl` in the west workspace:

```yaml
  remotes:
    - name: zephyrproject-rtos
      url-base: https://github.com/zephyrproject-rtos
    - name: etlcpp
      url-base: https://github.com/ETLCPP
  projects:
    - name: zephyr
      # …unchanged…
    - name: etl
      remote: etlcpp
      revision: 20.48.0
      path: modules/lib/etl
```

`west update` then fetches ETL and Zephyr auto-registers the module.

### 2. Enable + configure (Kconfig)

Add to `app/prj.conf`, `tests/sim_shell/prj.conf`, and the new `tests/etl/prj.conf`:

```
CONFIG_ETL=y
CONFIG_ETL_LOG_ERRORS=y
```

Exceptions/RTTI stay off: we do **not** enable `CONFIG_CPP_EXCEPTIONS` /
`CONFIG_CPP_RTTI`. With Zephyr's default `-fno-exceptions -fno-rtti`, ETL auto-detects and
emits no `throw`; instead it routes errors through the error handler (because
`ETL_LOG_ERRORS` is defined).

### 3. Error-handler glue (reusable TU)

New `app/src/etl_error_handler.cpp`:

- Registers an `etl::error_handler` callback via `SYS_INIT` (automatic, no `main()` edit).
- Callback logs `LOG_ERR("ETL error: %s @ %s:%d", e.what(), e.file_name(), e.line_number())`
  then calls `k_panic()` — fail-fast.
- References ETL symbols, so it also proves the link in the app build.

Compiled into: `app/CMakeLists.txt` (app + fm_board + native_sim app), the
`tests/sim_shell` build (which already compiles `app/src/main.cpp`), and the new
`tests/etl` build — all three compile `app/src/etl_error_handler.cpp`, so every ETL-enabled
build has exactly one registered handler.

### 4. Proof test — `tests/etl/` (Ztest, native_sim)

New isolated Ztest suite proving a real ETL type links and works:

- `tests/etl/CMakeLists.txt` (also compiles `app/src/etl_error_handler.cpp`),
  `tests/etl/prj.conf`, `tests/etl/testcase.yaml`, `tests/etl/src/main.cpp`.
- `prj.conf`: `CONFIG_ZTEST=y`, `CONFIG_CPP=y`, `CONFIG_STD_CPP20=y`,
  `CONFIG_EXTERNAL_LIBCPP=y`, `CONFIG_ETL=y`, `CONFIG_ETL_LOG_ERRORS=y`.
- Test cases exercise `etl::string<32>`: default construct, `append`, `operator==` /
  `compare`, `size()`/`capacity()` — deterministic, always-green assertions.
- We do **not** deliberately trip the overflow/error path (would panic the test);
  the error handler's correctness is covered by it being registered and compiling.

### 5. CI (`.github/workflows/ci.yml`)

Add one step to `build_and_tests`:

```yaml
      - name: Twister ETL proof (tests/etl)
        working-directory: fw
        shell: bash
        run: |
          set -euo pipefail
          west twister -T tests/etl -p native_sim/native/64 -v --inline-logs
```

`clang_format` already globs `tests/` — the new C++ must be pre-formatted (clang-format-18).
`fm_board` is **not** in CI today; that stays unchanged (scope discipline). `fm_board`
compilation is verified locally per the DoD.

## Verification (local, before PR)

Because the shared `.west` manifest path points at the main tree (where the parallel D1
session lives), local verification temporarily repoints west at this worktree, then
restores it:

1. `west config manifest.path FW-RemoteStation-etl` (save + restore the old value after).
2. `west update` → ETL at `modules/lib/etl` @ 20.48.0.
3. `west build -b native_sim/native/64 app -p always` — app links ETL.
4. `west build -b fm_board app -p always` — arm target compiles ETL (also flushes the
   known Zephyr `<new>`/placement-new risk).
5. `west twister -T tests/etl -p native_sim/native/64` — proof green.
6. `west twister -T tests/sim_shell -p native_sim/native/64` and `-T app --integration` —
   no regressions.
7. `clang-format` clean over changed files.
8. Restore `west config manifest.path FW-RemoteStation`.

## Definition of Done

- `west.yml`: ETL pinned to `20.48.0`; `west update` pulls it.
- CMake/Kconfig: ETL include + target linked in app **and** tests.
- ETL configured: no exceptions (error handler → `LOG_ERR`/`k_panic`), no RTTI, C++20,
  no-alloc.
- Proof: `tests/etl` uses `etl::string<32>` and runs green on `native_sim`; `fm_board`
  build compiles.
- CI green: `build_and_tests` + `clang_format`.
- One PR against `main` with `Closes #43`.

## Out of scope

- CLAUDE.md / CONTRIBUTING.md wording (separate session).
- Rewriting existing usage (SA818 refactor #40 is separate).
- Adding `fm_board` to CI.

## Process

brainstorming → writing-plans → subagent-driven-development (**forge**: west/CMake/CI;
**rhythm**: Zephyr error-handler + Ztest) → test-driven-development (proof test) →
verification-before-completion → one PR (`Closes #43`) → copilot-loop.
