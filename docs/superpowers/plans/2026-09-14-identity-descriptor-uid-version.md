# Identity-Descriptor: UID + FW-Version + Variante — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Die Firmware meldet im `MODULE-DESCRIBE` ihre STM32-UID, `uid_source`, echte FW-Version und Band-`variant`; Release-Assets werden auf `vhf`/`uhf` vereinheitlicht.

**Architecture:** Ein wiederverwendbarer UID-Baustein (`subsys/module/device_uid.*`) liefert (a) einen **reinen, ztest-baren Formatter**, der byte-identisch zu `scripts/read_uid.py::format_uid` ist, und (b) einen Provider, der die UID zur Laufzeit besorgt — real via Zephyr `hwinfo`, im `native_sim` als **persistierte synthetische** UID. Der generische `Identity`/`describe()`-Contract in `iface.h` wird um `variant`/`uid`/`uid_source` erweitert und `version` von Band auf FW-Version umgestellt (`schema` 1→2); das SA818-Modul verdrahtet die Laufzeitwerte.

**Tech Stack:** Zephyr RTOS (v4.4.2), C++, ztest/twister (native_sim), pytest (sim_shell), West build. Board `fm_board` (STM32U575) + `native_sim`.

**Spec:** `docs/superpowers/specs/2026-09-14-identity-descriptor-uid-version-design.md`

## Global Constraints

- **UID-Format-Zwang:** Laufzeit-UID-String MUSS byte-identisch zu `scripts/read_uid.py::format_uid` sein: 3×32-bit-Worte (gelesen an aufsteigenden Adressen 0x..0700/04/08), **High-Word zuerst (reversed)**, jeweils `%08X`, 24 Hex-Zeichen, upper-case. Kanonischer Vektor: `[0x11223344,0x55667788,0x99AABBCC] → "99AABBCC5566778811223344"`.
- **Varianten-Vokabular = `vhf`/`uhf`** überall (Descriptor `variant`, Release-Asset-Namen, station-manager).
- **`identity.version` = echte FW-Version** (`APP_VERSION`, Format `%02u.%02u.%02u-%02u` = `YY.MM.DD-NN`), NICHT die Band-Angabe.
- **`schema` 2** im Describe-JSON.
- **`uid_source`** ∈ {`stm32_uid` (real), `synthetic` (native_sim)}.
- Lizenz-Header wie im Repo (`SPDX-License-Identifier: LGPL-3.0-or-later`, `Copyright (c) 2026 OE5XRX`) in neuen Dateien.
- Real-HW-UID-Byteorder wird am Bench (HIL) bestätigt (deferred, wie E Task 7); CI deckt native_sim + reinen Formatter voll ab.
- Tests: `west twister -T tests/unit_device_uid` (ztest) und die sim_shell-pytest-Suite. Vor PR: bestehende Suites grün.

---

## File Structure

**Neu:**
- `subsys/module/device_uid.h` — API: `mod_format_uid`, `mod_device_uid`, `mod_uid_source`.
- `subsys/module/device_uid.cpp` — reiner Formatter + Provider (hwinfo real / synthetic native_sim).
- `tests/unit_device_uid/` — ztest: `testcase.yaml`, `CMakeLists.txt`, `prj.conf`, `src/main.cpp` (Formatter-Cross-Check).

**Modifiziert:**
- `include/oe5xrx/module/iface.h` — `Identity` (+`variant`,`uid`,`uid_source`); `describe()` (schema 2 + neue Felder).
- `subsys/module/devices/sa818/sa818_module.cpp` — Laufzeit-`version`/`variant`/`uid`/`uid_source` verdrahten.
- `subsys/module/CMakeLists.txt` — `device_uid.cpp` aufnehmen.
- `app/prj.conf` — `CONFIG_HWINFO=y`.
- `tests/sim_shell/pytest/test_module_iface.py` — Identity-Assertions.
- `release/release-targets.yaml` — Namen `…-2m`→`…-vhf`, `…-70cm`→`…-uhf`.
- Referenzen auf `2m`/`70cm` (release.yml, Doku, provision.sh) — grep-getrieben.

**Contract der neuen Felder** (Referenz für alle Tasks): `describe()` JSON `identity` = `{type, model, version, variant, uid, uid_source}`, `schema:2`. Für SA818: `type="fm_transceiver"`, `model="SA818-V"|"SA818-U"`, `version`=FW-Version, `variant="vhf"|"uhf"`, `uid`=24-hex (real) bzw. synthetisch, `uid_source="stm32_uid"|"synthetic"`.

---

## Task 1: Reiner UID-Formatter + ztest-Cross-Check

**Files:**
- Create: `subsys/module/device_uid.h`, `subsys/module/device_uid.cpp`
- Create: `tests/unit_device_uid/testcase.yaml`, `tests/unit_device_uid/CMakeLists.txt`, `tests/unit_device_uid/prj.conf`, `tests/unit_device_uid/src/main.cpp`
- Modify: `subsys/module/CMakeLists.txt`

**Interfaces:**
- Produces: `void mod_format_uid(const uint32_t words[3], char out[25]);` — schreibt 24 Hex + NUL, High-Word zuerst, upper-case. (Weitere Provider-Funktionen in Task 2.)

- [ ] **Step 1: Header anlegen** — `subsys/module/device_uid.h`:

```cpp
/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Device UID: canonical formatting (mirrors scripts/read_uid.py::format_uid)
 * plus a runtime provider (STM32 hwinfo on real HW, persisted-synthetic on
 * native_sim). The formatted string MUST match the bench provisioning UID.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

/** 3 words (ascending addresses) -> 24 upper-hex chars + NUL, high word first. */
void mod_format_uid(const uint32_t words[3], char out[25]);

/** Runtime device UID string (24 hex real / synthetic sim). Stable per boot. */
const char *mod_device_uid(void);

/** "stm32_uid" (real hwinfo) or "synthetic" (native_sim). */
const char *mod_uid_source(void);
```

- [ ] **Step 2: ztest schreiben (failing)** — `tests/unit_device_uid/src/main.cpp`:

```cpp
/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include <zephyr/ztest.h>
#include "device_uid.h"

ZTEST(device_uid, test_format_matches_read_uid_py)
{
    const uint32_t words[3] = {0x11223344u, 0x55667788u, 0x99AABBCCu};
    char out[25];
    mod_format_uid(words, out);
    zassert_str_equal(out, "99AABBCC5566778811223344");
}

ZTEST(device_uid, test_format_zero_padding)
{
    const uint32_t words[3] = {0x1u, 0x0u, 0x0u};
    char out[25];
    mod_format_uid(words, out);
    zassert_str_equal(out, "000000000000000000000001");
}

ZTEST_SUITE(device_uid, NULL, NULL, NULL, NULL, NULL);
```

`tests/unit_device_uid/testcase.yaml`:

```yaml
tests:
  module.device_uid:
    platform_allow: native_sim
    tags: module uid
```

`tests/unit_device_uid/prj.conf`:

```
CONFIG_ZTEST=y
CONFIG_CPP=y
CONFIG_STD_CPP20=y
```

`tests/unit_device_uid/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(device_uid_test)
target_sources(app PRIVATE src/main.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../../subsys/module/device_uid.cpp)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../subsys/module)
```

> Step 1 des Ausführenden: bestehende `tests/etl/` bzw. `tests/unit_audio/` als Vorlage für die exakte C++-ztest/CMake/prj.conf-Konvention dieses Repos gegenchecken und angleichen (STD_CPP-Version, find_package-Zeile).

- [ ] **Step 3: Run → FAIL** — `west twister -p native_sim -T tests/unit_device_uid` → Build/Link-Fehler (nur Formatter-Stub fehlt).

- [ ] **Step 4: Formatter implementieren** — `subsys/module/device_uid.cpp` (nur der reine Teil; Provider folgt Task 2):

```cpp
/*
 * Copyright (c) 2026 OE5XRX
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "device_uid.h"
#include <stdio.h>

void mod_format_uid(const uint32_t words[3], char out[25])
{
    // High word first (reversed), each as 8 upper-hex — mirrors read_uid.py.
    snprintf(out, 25, "%08X%08X%08X",
             (unsigned)words[2], (unsigned)words[1], (unsigned)words[0]);
}
```

- [ ] **Step 5: Run → PASS** — `west twister -p native_sim -T tests/unit_device_uid` → beide ZTESTs grün.

- [ ] **Step 6: Cross-Check gegen Python** — sicherstellen, dass `python -m pytest scripts/tests/test_read_uid.py` weiterhin grün ist (gleiche Vektoren). Beide Seiten produzieren `"99AABBCC5566778811223344"`.

- [ ] **Step 7: Commit**

```bash
git add subsys/module/device_uid.h subsys/module/device_uid.cpp tests/unit_device_uid/ subsys/module/CMakeLists.txt
git commit -m "feat(module): canonical UID formatter matching read_uid.py + ztest"
```

---

## Task 2: UID-Provider — synthetic (native_sim) + hwinfo (real)

**Files:**
- Modify: `subsys/module/device_uid.cpp`
- Modify: `app/prj.conf` (`CONFIG_HWINFO=y`)
- Test: `tests/sim_shell/pytest/test_module_iface.py` (UID-Stabilität — hinzugefügt in Task 3; hier native_sim-Provider-Verhalten)

**Interfaces:**
- Produces: `const char *mod_device_uid(void)` (stabil pro Boot; native_sim persistiert über Neustarts), `const char *mod_uid_source(void)`.
- Consumes: `mod_format_uid` (Task 1).

- [ ] **Step 1: `CONFIG_HWINFO=y`** in `app/prj.conf` ergänzen (real-HW-Pfad; native_sim ignoriert es bzw. liefert -ENOSYS → synthetic-Fallback).

- [ ] **Step 2: Provider implementieren** — in `subsys/module/device_uid.cpp` ergänzen:

```cpp
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <string.h>

static char s_uid[25];
static const char *s_uid_source = "stm32_uid";
static bool s_uid_ready;

#if defined(CONFIG_BOARD_NATIVE_SIM)
#include <stdio.h>
// native_sim: persist a once-generated synthetic UID to a host file so it is
// stable across restarts (A/B/C sim tests rely on a stable identity).
static const char *SIM_UID_PATH = "oe5xrx_sim_uid.txt";

static void load_or_make_synthetic(void)
{
    FILE *f = fopen(SIM_UID_PATH, "r");
    if (f) {
        if (fgets(s_uid, sizeof(s_uid), f) && strlen(s_uid) >= 24) {
            s_uid[24] = '\0';
            fclose(f);
            s_uid_source = "synthetic";
            return;
        }
        fclose(f);
    }
    uint32_t w[3];
    sys_rand_get(w, sizeof(w));
    mod_format_uid(w, s_uid);
    f = fopen(SIM_UID_PATH, "w");
    if (f) { fputs(s_uid, f); fclose(f); }
    s_uid_source = "synthetic";
}
#else
#include <zephyr/drivers/hwinfo.h>
// Real HW: read the STM32 96-bit UID and format identically to read_uid.py.
// hwinfo returns raw bytes; interpret as 3 little-endian words at offsets
// 0/4/8 (same as read_uid.py's read32 at ascending addresses).
// NOTE: exact byte/word order confirmed on real silicon at the bench (HIL).
static void load_hwinfo_uid(void)
{
    uint8_t buf[12] = {0};
    ssize_t n = hwinfo_get_device_id(buf, sizeof(buf));
    if (n < (ssize_t)sizeof(buf)) {
        strcpy(s_uid, "000000000000000000000000");
        s_uid_source = "stm32_uid";
        return;
    }
    uint32_t w[3];
    for (int i = 0; i < 3; i++) {
        w[i] = (uint32_t)buf[i*4] | ((uint32_t)buf[i*4+1] << 8) |
               ((uint32_t)buf[i*4+2] << 16) | ((uint32_t)buf[i*4+3] << 24);
    }
    mod_format_uid(w, s_uid);
    s_uid_source = "stm32_uid";
}
#endif

const char *mod_device_uid(void)
{
    if (!s_uid_ready) {
#if defined(CONFIG_BOARD_NATIVE_SIM)
        load_or_make_synthetic();
#else
        load_hwinfo_uid();
#endif
        s_uid_ready = true;
    }
    return s_uid;
}

const char *mod_uid_source(void)
{
    (void)mod_device_uid();  // ensure s_uid_source is set
    return s_uid_source;
}
```

> Step 1 des Ausführenden: prüfen, ob `sys_rand_get`/`<zephyr/random/random.h>` im native_sim-Build verfügbar ist (sonst `CONFIG_TEST_RANDOM_GENERATOR=y`/`CONFIG_ENTROPY_GENERATOR` bzw. `sys_rand32_get`). CWD-Persistenz-Pfad ggf. an die Twister-/Runner-Arbeitsverzeichnis-Konvention anpassen.

- [ ] **Step 3: Build-Sanity** — `west build -b native_sim app` baut fehlerfrei; `west build -b fm_board app` baut fehlerfrei (hwinfo verlinkt).

- [ ] **Step 4: Commit**

```bash
git add subsys/module/device_uid.cpp app/prj.conf
git commit -m "feat(module): runtime UID provider — synthetic (native_sim) + hwinfo (real)"
```

---

## Task 3: Identity-Contract erweitern + SA818 verdrahten (schema 2)

**Files:**
- Modify: `include/oe5xrx/module/iface.h` (Identity + describe)
- Modify: `subsys/module/devices/sa818/sa818_module.cpp` (Laufzeitwerte)
- Modify: `tests/sim_shell/pytest/test_module_iface.py`

**Interfaces:**
- Consumes: `mod_device_uid`, `mod_uid_source` (Task 2).
- Produces: Describe-JSON `identity{type,model,version,variant,uid,uid_source}`, `schema:2`.

- [ ] **Step 1: sim_shell-Test anpassen (failing)** — in `tests/sim_shell/pytest/test_module_iface.py`, `test_module_describe_valid_json`:

```python
    assert d["schema"] == 2
    assert d["module"] == "fm"
    assert d["identity"]["type"] == "fm_transceiver"
    assert d["identity"]["model"] == "SA818-V"
    # version is now the firmware version (YY.MM.DD-NN), not the band
    import re
    assert re.fullmatch(r"\d{2}\.\d{2}\.\d{2}-\d{2}", d["identity"]["version"])
    assert d["identity"]["variant"] == "vhf"
    assert re.fullmatch(r"[0-9A-F]{24}", d["identity"]["uid"])
    assert d["identity"]["uid_source"] == "synthetic"
```

- [ ] **Step 2: Run → FAIL** — `west twister -p native_sim -T tests/sim_shell` (bzw. der etablierte sim_shell-Runner) → schema/variant/uid-Assertions failen.

- [ ] **Step 3: `Identity` erweitern** — `include/oe5xrx/module/iface.h`:

```cpp
struct Identity {
  const char *type;
  const char *model;
  const char *version;
  const char *variant;
  const char *uid;
  const char *uid_source;
};
```

- [ ] **Step 4: `describe()` erweitern** — im Identity-Block von `Module::describe()` (`schema` auf `"2"`, Felder ergänzen):

```cpp
    w.kvRaw("schema", "2");
    ...
    w.key("identity");
    w.ch('{');
    w.kvStr("type", id_.type);
    w.ch(',');
    w.kvStr("model", id_.model);
    w.ch(',');
    w.kvStr("version", id_.version);
    w.ch(',');
    w.kvStr("variant", id_.variant);
    w.ch(',');
    w.kvStr("uid", id_.uid);
    w.ch(',');
    w.kvStr("uid_source", id_.uid_source);
    w.ch('}');
```

- [ ] **Step 5: SA818 verdrahten** — `subsys/module/devices/sa818/sa818_module.cpp`: FW-Version-Buffer füllen, UID/variant setzen. `#include <zephyr/app_version.h>` und `#include "device_uid.h"` (Pfad ggf. relativ) ergänzen; g_identity umbauen:

```cpp
static char s_fw_version[16];
static int build_fw_version(void)
{
    snprintf(s_fw_version, sizeof(s_fw_version), "%02u.%02u.%02u-%02u",
             APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_PATCHLEVEL, APP_TWEAK);
    return 0;
}
SYS_INIT(build_fw_version, APPLICATION, 0);

// variant == BAND_NAME ("vhf"/"uhf"); model == BAND_MODEL; uid/source at runtime.
const Identity g_identity{
    "fm_transceiver", BAND_MODEL, s_fw_version, BAND_NAME,
    mod_device_uid(), mod_uid_source(),
};
```

> Achtung Init-Reihenfolge: `mod_device_uid()`/`mod_uid_source()` liefern Zeiger auf **statische, lazy-gefüllte** Buffer (Task 2) — bei statischer Initialisierung von `g_identity` ist der Zeiger gültig, der Inhalt wird spätestens beim ersten Aufruf (vor dem ersten `describe` zur Laufzeit) befüllt. `s_fw_version` wird per `SYS_INIT` vor dem Shell-Betrieb gefüllt. Falls die statische Init-Order Probleme macht: `g_identity`-Felder in einem `SYS_INIT`-Hook setzen, statt im Aggregat-Initializer. Der Ausführende verifiziert per native_sim-Run, dass `version`/`uid` im Describe nicht leer sind.

- [ ] **Step 6: Run → PASS** — sim_shell-Suite grün (schema 2, version=FW-Version, variant=vhf, uid 24-hex, uid_source=synthetic).

- [ ] **Step 7: UID-Persistenz-Test** — kleinen pytest-Fall ergänzen: zweimal `module fm describe` über zwei native_sim-Starts (bzw. wie der Runner Neustart abbildet) → gleiche `uid`. Falls der sim_shell-Runner keinen echten Neustart abbildet, mindestens innerhalb eines Laufs Stabilität + Vorhandensein der Persistenz-Datei prüfen.

- [ ] **Step 8: Commit**

```bash
git add include/oe5xrx/module/iface.h subsys/module/devices/sa818/sa818_module.cpp tests/sim_shell/pytest/test_module_iface.py
git commit -m "feat(module): identity descriptor schema 2 — uid/uid_source/variant + fw version"
```

---

## Task 4: Release-Assets auf `vhf`/`uhf` umbenennen

**Files:**
- Modify: `release/release-targets.yaml`
- Modify: alle Referenzen auf `2m`/`70cm` (grep-getrieben): `.github/workflows/release.yml`, Doku unter `docs/`, `scripts/provision.sh`, README-Beispiele.

**Interfaces:**
- Produces: Release-Asset-Basenamen `fm-sa818-vhf`, `fm-sa818-uhf` (statt `-2m`/`-70cm`).

- [ ] **Step 1: Referenzen finden** — `git grep -nE "sa818-2m|sa818-70cm|fm-sa818-2m|fm-sa818-70cm|\b2m\b|\b70cm\b"` — Liste aller Fundstellen erstellen.

- [ ] **Step 2: `release-targets.yaml` umbenennen**:

```yaml
targets:
  - name: fm-sa818-vhf
    board: fm_board
    band: vhf
    artifacts: [firmware, native_sim]
  - name: fm-sa818-uhf
    board: fm_board
    band: uhf
    artifacts: [firmware, native_sim]
```

Den Kommentar im Header (`fm-sa818-2m -> …`) entsprechend auf `fm-sa818-vhf` aktualisieren.

- [ ] **Step 3: Übrige Referenzen aktualisieren** — jede in Step 1 gefundene Stelle auf `vhf`/`uhf` umstellen (Workflow-Kommentare/Matrix, Doku-Beispiele, `provision.sh`-Beispielaufrufe). Keine multi-line-Kommentar-Fallen; Bänder-Semantik (`134–174` = vhf, `400–480` = uhf) beibehalten.

- [ ] **Step 4: Verifikation** — erneuter `git grep -nE "sa818-2m|sa818-70cm|\b2m\b|\b70cm\b"` liefert **keine** funktionalen Treffer mehr (reine Frequenz-Klartexte wie „2 m Band" in Prosa sind ok, aber Asset-/Target-Namen nicht). Falls Tests/Checks Asset-Namen prüfen: mitziehen.

- [ ] **Step 5: Dry-Run-Release (optional, empfohlen)** — sofern lokal/CI möglich: `release.yml` mit `workflow_dispatch dry_run=true` zeigt Asset-Namen `fm-sa818-vhf.*` / `fm-sa818-uhf.*`. Andernfalls im PR notieren, dass der erste echte Release die neuen Namen erzeugt.

- [ ] **Step 6: Commit**

```bash
git add release/release-targets.yaml .github/workflows/release.yml docs/ scripts/provision.sh
git commit -m "chore(release): rename fm-sa818 assets 2m/70cm -> vhf/uhf (unify variant vocab)"
```

---

## Abschluss

- [ ] **Volle Test-Suites grün:** `west twister -p native_sim -T tests/unit_device_uid -T tests/sim_shell` (+ bestehende Suites, die berührt sein könnten: `tests/etl`, `tests/boot_confirm`), und `python -m pytest scripts/tests`.
- [ ] **Real-HW-Byteorder** (`hwinfo` → words) als Bench/HIL-Punkt im PR notieren (deferred, wie E Task 7) — CI beweist native_sim + Formatter, echtes fm_board-UID-Byteorder wird am Bench bestätigt.
- [ ] **Cross-Repo-Notiz** im PR: station-manager A ingestet künftig echte `uid`/`version`/`variant`; `schema` 1→2 — kein A-Codewechsel nötig (A liest identity-Felder, nicht `schema`). C nutzt `version` (Ist/Soll) + `variant`.

## Self-Review-Notiz (bereits durchgeführt)

- **Spec-Abdeckung:** UID+uid_source (T1/T2/T3), version=FW-Version (T3), variant (T3), schema 2 (T3), Formatter==read_uid.py (T1, kanonischer Vektor), native_sim synthetic persistiert (T2), Asset-Rename vhf/uhf (T4), Sim-Tests (T3). Real-HW-UID-Byteorder als HIL deferred (Abschluss).
- **Type-Konsistenz:** `mod_format_uid(const uint32_t[3], char[25])`, `mod_device_uid()`, `mod_uid_source()` durchgängig T1→T3; `Identity`-Feldreihenfolge in Struct (T3 Step 3) == Aggregat-Initializer (T3 Step 5) == describe()-Serialisierung (T3 Step 4).
- **Verifikationspflichten:** ztest/CMake-Konvention (T1 Step 2), `sys_rand_get`-Verfügbarkeit + Persistenzpfad (T2 Step 2), statische Init-Order von `g_identity` (T3 Step 5), sim_shell-Runner-Neustart-Semantik (T3 Step 7) — je explizit als „verifizieren" markiert.
