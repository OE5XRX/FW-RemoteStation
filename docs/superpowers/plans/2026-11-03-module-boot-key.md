# Module Boot Key in MODULE-DESCRIBE Implementation Plan (FW-RemoteStation)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **Repo:** this plan was written in station-manager (`docs/superpowers/plans/`) and is meant to be **copied into FW-RemoteStation** (`docs/superpowers/plans/2026-11-03-module-boot-key.md`) on a fresh feature branch off `origin/main`, e.g. `feature/module-boot-key`. One PR (spec §12 PR 2).

**Goal:** `module <id> describe` reports `identity.boot_key`: the first 16 hex chars of the SHA-256 KEYHASH TLV of the running MCUboot image. This lets station-manager detect a bootloader/release key mismatch before it flashes.

**Architecture:** A pure, device-independent parser (`subsys/module/boot_key.{h,cpp}`) walks an MCUboot image header and its TLV areas through a read callback. On a MCUboot build (`CONFIG_BOOTLOADER_MCUBOOT`), a thin glue reads `slot0_partition` via the flash map at `module_identity_init` time and fills a static buffer. `Identity` gets an optional `boot_key` pointer; `Module::describe` emits the key only when non-empty. `schema` stays `2`.

**Tech Stack:** Zephyr (pinned SRCREV of this repo), C++ (ETL, no heap), ztest on `native_sim/native/64`, twister, imgtool 2.5.0 (fixture generation), pytest sim_shell harness, HIL bench.

**Spec:** station-manager `docs/superpowers/specs/2026-11-03-module-fw-update-state-design.md` (§7, §11.8). Server counterpart: station-manager plan `2026-11-03-module-fw-update-state.md`.

## Global Constraints

- Field name `boot_key`, inside `identity`. Value = **lowercase hex, exactly 16 chars** = first 8 bytes of KEYHASH TLV (type `0x01`, len 32). Server normalises too, but emit lowercase.
- MCUboot dev key (`root-ec-p256.pem`) → `e30466f6b8470c1f` (verified with imgtool `dumpinfo`). Release builds (prod key from secret) → a different value.
- No MCUboot (bare/debug build, native_sim), wrong magic, or no KEYHASH TLV → **omit the field** (no empty string, no `null`).
- `schema` stays `2`. Every other describe byte is unchanged (additive).
- Shell-thread stack budget: `subsys/module` builds with `-Werror=stack-usage=1024`. Read buffers are small (≤ 64 B on stack) or `static`.
- Never parse with unbounded offsets: every read is bounds-checked against the partition size.
- No release tag without explicit user approval (spec §10).

## Review Focus

1. **Swap-using-offset layout** (`SB_CONFIG_MCUBOOT_MODE_SWAP_USING_OFFSET=y`). Expected: the *running* image is read at offset 0 of `slot0_partition`. If the magic is not found there, the field is omitted (never a wrong key). The HIL step (Task 3) verifies this on real HW after an OTA swap, not just after a SWD flash.
2. **Image with protected TLV area** (`ih_protect_tlv_size != 0`, e.g. with `--boot-record` / security counter). Expected: KEYHASH is still found. Test `test_protected_tlv_area_is_skipped`.
3. **Corrupt or erased slot header** (all `0xFF`) or a TLV length pointing past the partition. Expected: field omitted, no fault. Tests `test_erased_header_returns_absent`, `test_tlv_len_overflow_returns_absent`.
4. **Flash read failure at init** (flash driver not ready). Expected: field omitted, boot continues, and the boot-confirm health gate is unaffected. Covered by the glue design (return code ignored → empty buffer). Reviewer checks that it adds no `__ASSERT`/`k_panic` path.
5. **Describe JSON buffer headroom.** The extra `,"boot_key":"…"` (≈ 30 B) must not truncate `describe` on fm_board. Test `test_module_describe_valid_json` stays green, plus the HIL describe parse in Task 3.

---

## File Structure

| File | Responsibility |
|---|---|
| `subsys/module/boot_key.h` / `boot_key.cpp` (create) | `mod::boot_key_from_image(read_fn, ctx, size, out)`: pure TLV walk |
| `subsys/module/boot_key_flash.cpp` (create) | `mod_boot_key_init(char *out)`: flash-map glue, compiled only with `CONFIG_BOOTLOADER_MCUBOOT` |
| `subsys/module/CMakeLists.txt` (modify) | add sources (`_ifdef CONFIG_MODULE_SA818` / `CONFIG_BOOTLOADER_MCUBOOT`) |
| `include/oe5xrx/module/iface.h` (modify) | `Identity::boot_key` + conditional emit in `Module::describe` |
| `subsys/module/devices/sa818/sa818_module.cpp` (modify) | `s_boot_key[17]`, fill in `module_identity_init`, pass to `g_identity` |
| `tests/unit_boot_key/` (create) | ztest of the parser against imgtool-signed fixtures |
| `tests/sim_shell/pytest/test_module_iface.py` (modify) | native_sim: `boot_key` absent |
| `.github/workflows/ci.yml` (modify) | twister step for `tests/unit_boot_key` |
| `scripts/hil_bench_gate.py` or `tests/sim_shell/pytest/test_sa818_hil.py` (modify, whichever already parses `describe` on HW) | HIL assertion of `boot_key` |

---

### Task 1: Pure KEYHASH parser + ztest

**Files:**
- Create: `subsys/module/boot_key.h`, `subsys/module/boot_key.cpp`
- Create: `tests/unit_boot_key/{CMakeLists.txt,prj.conf,testcase.yaml,src/main.cpp}`, `tests/unit_boot_key/fixtures/{make_fixtures.sh,dev-signed.bin,other-signed.bin,prot-signed.bin,other-key.pem,root-ec-p256.pem}`
- Modify: `subsys/module/CMakeLists.txt`, `.github/workflows/ci.yml`

**Interfaces:**
- Produces:

```cpp
namespace mod {
/** Read @p len bytes at @p off into @p dst; return 0 on success, <0 on error. */
using ImageReadFn = int (*)(void *ctx, size_t off, void *dst, size_t len);
inline constexpr size_t kBootKeyHexLen = 16;
/** Fill @p out (kBootKeyHexLen + 1 bytes) with the lowercase hex prefix of the KEYHASH
 *  TLV of the MCUboot image readable via @p read (image starts at offset 0, @p size bytes
 *  readable). Returns true on success; on any failure returns false and sets out[0] = '\0'. */
bool boot_key_from_image(ImageReadFn read, void *ctx, size_t size, char *out);
}
```

The algorithm is fixed by the MCUboot image format (little-endian). Header: `magic u32 @0 == 0x96F3B83D`, `ih_hdr_size u16 @8`, `ih_protect_tlv_size u16 @10`, `ih_img_size u32 @12`. TLV start = `hdr_size + img_size`. If `u16 @start == 0x6908` (protected info), skip its `u16 total_len`. The next info must be `0x6907` with `total_len` (including the 4-byte info). Walk the entries `u16 type, u16 len, data`. On type `0x0001` with `len == 32`, read the first 8 data bytes and hex-encode them. Any offset/length that exceeds `size` → false.

- [ ] **Step 1: Generate fixtures** (`make_fixtures.sh`, checked in; `root-ec-p256.pem` is MCUboot's public dev key, sha256 `1734d7ee76344fbf2b1731f49baa5e1ced9bea56b66fe84703b1a492e0a13614`. `other-key.pem` is a throwaway created once via `imgtool keygen -t ecdsa-p256`):

```bash
IMG="uvx --from imgtool==2.5.0 imgtool"
head -c 2048 /dev/zero | tr '\0' '\125' > /tmp/payload.bin
$IMG sign --key root-ec-p256.pem --header-size 0x200 --pad-header --align 4 --version 1.2.3 --slot-size 0x20000 /tmp/payload.bin dev-signed.bin
$IMG sign --key other-key.pem   --header-size 0x200 --pad-header --align 4 --version 1.2.3 --slot-size 0x20000 /tmp/payload.bin other-signed.bin
$IMG sign --key root-ec-p256.pem --header-size 0x200 --pad-header --align 4 --version 1.2.3 --slot-size 0x20000 --security-counter 7 /tmp/payload.bin prot-signed.bin
```

Confirm with `imgtool dumpinfo`: dev KEYHASH starts `e3 04 66 f6 b8 47 0c 1f`, and `prot-signed.bin` has a protected TLV area. Record the `other-signed.bin` prefix as a constant in the test. Embed the fixtures with `generate_inc_file_for_target(app fixtures/<name>.bin ${ZEPHYR_BINARY_DIR}/include/generated/<name>.inc)`.

- [ ] **Step 2: Write failing ztests** (`tests/unit_boot_key/src/main.cpp`, buffer-backed `ImageReadFn`)

```cpp
ZTEST(boot_key, test_dev_signed_yields_dev_prefix)      { /* zassert_true(ok); zassert_str_equal(out, "e30466f6b8470c1f"); */ }
ZTEST(boot_key, test_other_key_yields_other_prefix)     { /* == recorded other prefix, != dev */ }
ZTEST(boot_key, test_protected_tlv_area_is_skipped)     { /* prot-signed.bin → dev prefix */ }
ZTEST(boot_key, test_erased_header_returns_absent)      { /* 0xFF × 512 → false, out[0] == '\0' */ }
ZTEST(boot_key, test_truncated_image_returns_absent)    { /* size = 600 → false */ }
ZTEST(boot_key, test_tlv_len_overflow_returns_absent)   { /* patch info total_len = 0xFFFF → false */ }
ZTEST(boot_key, test_no_keyhash_tlv_returns_absent)     { /* patch KEYHASH type → 0x7F → false */ }
ZTEST(boot_key, test_read_error_returns_absent)         { /* read fn returns -EIO → false */ }
```

`testcase.yaml`: `module.boot_key`, `platform_allow: native_sim/native/64`, `harness: ztest`, tags `module, mcuboot` (pattern: `tests/unit_device_uid`).

- [ ] **Step 3: Run, expect FAIL:** `west twister -T tests/unit_boot_key -p native_sim/native/64 -v --inline-logs` → build error (missing source).
- [ ] **Step 4: Implement** `boot_key.cpp` (no heap, no Zephyr deps beyond `<stdint.h>/<string.h>`). Add it to `subsys/module/CMakeLists.txt` under `CONFIG_MODULE_SA818`, matching `device_uid.cpp`.
- [ ] **Step 5: Run, expect PASS.** Add the CI step `Twister boot-key parser (tests/unit_boot_key)` next to the boot_confirm step in `ci.yml`.
- [ ] **Step 6: Commit** — `feat(module): MCUboot KEYHASH prefix parser (+ ztest against imgtool fixtures)`

---

### Task 2: Wire `boot_key` into identity + describe

**Files:**
- Create: `subsys/module/boot_key_flash.cpp`
- Modify: `include/oe5xrx/module/iface.h` (`struct Identity`, `Module::describe`)
- Modify: `subsys/module/devices/sa818/sa818_module.cpp` (identity buffers, `module_identity_init`, `g_identity`)
- Modify: `subsys/module/CMakeLists.txt`
- Test: `tests/sim_shell/pytest/test_module_iface.py`

**Interfaces:**
- Consumes: `mod::boot_key_from_image` (Task 1)
- Produces:
  - `struct Identity { …existing six…; const char *boot_key = nullptr; }`: last member, so the existing aggregate init stays valid. `nullptr` or `""` → omitted.
  - `Module::describe` emits `,"boot_key":"<value>"` right after `uid_source` and only if `id_.boot_key && id_.boot_key[0]`.
  - `void mod_boot_key_init(char *out /* kBootKeyHexLen+1 */)`: in `boot_key_flash.cpp`, built only with `CONFIG_BOOTLOADER_MCUBOOT`. It opens `FIXED_PARTITION_ID(slot0_partition)` via `flash_area_open`, wraps `flash_area_read` as `ImageReadFn`, passes `fa->fa_size`, and closes the area. On any error `out[0] = '\0'`. Without MCUboot, `sa818_module.cpp` does not call it (`#if defined(CONFIG_BOOTLOADER_MCUBOOT)`), so `s_boot_key` stays empty.
  - `sa818_module.cpp`: `char s_boot_key[mod::kBootKeyHexLen + 1];`, filled in `module_identity_init()` (APPLICATION init, before the shell serves describe). Pass it as the 7th `g_identity` initializer.

- [ ] **Step 1: Write failing/pinning tests**

```python
# tests/sim_shell/pytest/test_module_iface.py
def test_module_describe_omits_boot_key_without_mcuboot(shell):
    d = _describe(shell)            # existing helper used by test_module_describe_valid_json
    assert "boot_key" not in d["identity"]
    assert d["schema"] == 2
```

`test_module_describe_valid_json` must stay green unchanged (additive contract).

- [ ] **Step 2: Implement** per the Interfaces block. Pull in the Kconfig dependency only on MCUboot builds: `FLASH_MAP` is already implied by the IMG_MANAGER config used by `boot_confirm_fm.cpp` (`zephyr/dfu/mcuboot.h`). If not, add `select FLASH_MAP if BOOTLOADER_MCUBOOT` to `MODULE_SA818`.
- [ ] **Step 3: Run:**
  - `west twister -T tests/sim_shell -p native_sim/native/64 -v --inline-logs` → PASS
  - `west twister -T tests/unit_boot_key -p native_sim/native/64 -v --inline-logs` → PASS
  - `west build -b fm_board/stm32u575xx app -p always` (bare) → builds, no `-Werror=stack-usage` failure
  - `west build -b fm_board --sysbuild app -d build_release -p always` → builds
  - `imgtool dumpinfo build_release/app/zephyr/zephyr.signed.bin` → KEYHASH starts `e3 04 66 f6 b8 47 0c 1f` (dev key). Note it for Task 3.
- [ ] **Step 4: Commit** — `feat(module): report boot_key (MCUboot KEYHASH prefix) in MODULE-DESCRIBE identity`

---

### Task 3: HIL verification on fm_board (spec §11.8)

**Bench discipline (non-negotiable; details and access live only in the private meta notes, never in this repo):** exactly **one** debug-probe access at a time, serial, in the foreground. Never run parallel or backgrounded flash/reset calls. No probe access while audio tools run. Follow the documented wedge-recovery order. After the test, reset the bench checkout to `main` / pristine pinned Zephyr. Build and flash as the unprivileged bench user, per `docs/hil-runner-setup.md`.

**Files:**
- Modify: the existing HW describe check (`scripts/hil_bench_gate.py` if it parses `describe`, else `tests/sim_shell/pytest/test_sa818_hil.py`), plus the matching `hil.yml` job if needed

**Interfaces:**
- Consumes: Task 2 firmware; `imgtool dumpinfo` of the flashed artifact

- [ ] **Step 1: Add the HW assertion** (runs in the `hil-ok`-gated workflow):

```python
def test_describe_reports_boot_key_matching_image(...):
    d = describe_on_hw("fm")
    assert re.fullmatch(r"[0-9a-f]{16}", d["identity"]["boot_key"])
    assert d["identity"]["boot_key"] == keyhash_prefix(signed_bin_path)  # parsed from the flashed .signed.bin (same TLV walk, Python)
```

- [ ] **Step 2: Manual bench run (MCUboot build):** build `--sysbuild` (dev key) and flash via SWD (one call). Open the CDC shell and run `module fm describe`. Expected: `"boot_key":"e30466f6b8470c1f"`, equal to `imgtool dumpinfo` KEYHASH[0:8] of the flashed `zephyr.signed.bin`.
- [ ] **Step 3: OTA-swap path (Review Focus 1):** DFU-upload a second signed build (bumped tweak version) to the secondary slot, let MCUboot swap (test build with `CONFIG_FM_BOOT_CONFIRM_SKIP_SA818=y` so it confirms without 12 V), then run `module fm describe` again. Expected: the same `boot_key` and the new version.
- [ ] **Step 4: Bare build:** `west build -b fm_board app` and flash. Expected: `describe` has **no** `boot_key`, and the rest is unchanged.
- [ ] **Step 5: Restore the bench** to `main` + pristine Zephyr pin. Paste the describe outputs (minus UID) and the imgtool KEYHASH line into the PR description.
- [ ] **Step 6: Commit** — `test(hil): assert describe boot_key matches flashed image KEYHASH`

---

## Delivery

One PR in FW-RemoteStation (`hil-ok` label only after diff review incl. `.github/workflows/`). It takes effect in the field only with a new FW release (tag needs **user approval**). Station-manager tolerates the missing field, so merge order does not matter.
