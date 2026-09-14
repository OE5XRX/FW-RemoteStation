# Identity-Descriptor: UID + FW-Version + Variante — Design

**Programm:** Modul-Firmware-Update (Overview liegt in `station-manager`:
`docs/superpowers/specs/2026-09-12-module-firmware-update-design.md`)
**Repo:** FW-RemoteStation (Zephyr, STM32U5 / fm_board)
**Datum:** 2026-09-14
**Status:** Design in Review (awaiting approval → implementation plan)

## Ziel

Die Firmware soll im Self-Describe-Protokoll (`module <id> describe` →
`MODULE-DESCRIBE {json}`) ihre **physische Identität und ihren echten Firmware-Stand
zur Laufzeit** melden: die **STM32-96-bit-UID**, die **tatsächliche Firmware-Version**
und die **Band-Variante**. Heute fehlt die UID komplett, und `identity.version` trägt
fälschlich die Band-Angabe statt der FW-Version.

Das ist der fehlende Baustein, auf dem die station-manager-Seite des Programms
aufsetzt: **A** (Modul-Inventar) matcht das physische Modul über die UID und zeigt die
gemeldete FW-Version; **C** (Reconciler) vergleicht gemeldete Ist-Version gegen die
Soll-Version; **B** (Release-Management) unterscheidet Band-Varianten. Der Agent reicht
`identity{}` unverändert durch — es ist reine Firmware-Arbeit.

## Scope

**Im Scope (FW-RemoteStation):**
- `identity`-Struct + `MODULE-DESCRIBE`-JSON um **`uid`**, **`uid_source`**,
  **`variant`** erweitern; **`version`** von Band auf **echte FW-Version** umstellen.
- Laufzeit-UID auf **fm_board** via Zephyr `hwinfo_get_device_id()`, formatiert
  **byte-identisch zu `scripts/read_uid.py`** (24-hex, big-endian Wortordnung).
- **native_sim**: stabile, persistierte **synthetische UID** + `uid_source=synthetic`.
- **FW-Version** aus `APP_VERSION` (dieselbe Quelle wie `version_shell.cpp`).
- **`variant`** aus dem DT-Band (`vhf`/`uhf`).
- **Release-Assets auf `vhf`/`uhf` umbenennen** (`fm-sa818-2m`→`fm-sa818-vhf`,
  `fm-sa818-70cm`→`fm-sa818-uhf`) in `release/release-targets.yaml` + allen Referenzen
  (`release.yml`, Doku, `provision.sh`-Beispiele), damit das Varianten-Vokabular
  durchgängig `vhf`/`uhf` ist (Descriptor ↔ Release-Asset ↔ B).
- Sim-Tests (`tests/sim_shell/pytest/test_module_iface.py` u.a.) anpassen.

**Bewusst NICHT im Scope:**
- Release-/Signing-Pipeline selbst (existiert, PR #49/#60/#64 — nur Asset-Namen ändern).
- OTA-Flash-Ausführung (Teilbereich D), station-manager-Änderungen (A/B/C).
- Änderung des Modul-Shell-Protokolls jenseits der `identity`-Felder.

## Getroffene Entscheidungen (Brainstorm 2026-09-14)

1. **`identity.version` = echte FW-Version** (z.B. `26.07.04-01`, aus `APP_VERSION`);
   die Band-Angabe wandert in ein neues Feld **`variant`**. (Heute trägt `version`
   die Band-Angabe — semantisch falsch für A/C.)
2. **Kanonisches Varianten-Vokabular = `vhf`/`uhf`** überall, inkl. Umbenennung der
   Release-Assets (`…-2m`→`…-vhf`, `…-70cm`→`…-uhf`).
3. **UID zur Laufzeit** aus dem echten STM32-UID (real) bzw. synthetisch (sim);
   `uid_source` ∈ {`stm32_uid`, `synthetic`}.
4. **UID-Format-Zwang:** Der Laufzeit-UID-String **muss** exakt dem entsprechen, was
   `scripts/read_uid.py` erzeugt und `provision-<uid>.json` enthält (24-hex,
   big-endian, High-Word zuerst) — sonst matcht A ein bench-registriertes Modul nicht
   auf seinen Heartbeat.

## Ausgangszustand (heute)

`module fm describe` liefert (Quelle: `subsys/module/devices/sa818/sa818_module.cpp`
~Z.581, Struct in `include/oe5xrx/module/iface.h` ~Z.445):

```json
{
  "schema": 1,
  "module": "fm",
  "identity": { "type": "fm_transceiver", "model": "SA818-V", "version": "vhf" },
  "capabilities": [ ... ]
}
```

- `identity.version` = Band (`vhf`/`uhf`), **nicht** die FW-Version.
- **keine** `uid` / `uid_source`.
- Echte FW-Version nur separat über `version`-Shell-Command (`app/src/version_shell.cpp`
  → `APP-VERSION 26.07.04-01`).
- UID nur beim Bench-Provisioning (`scripts/read_uid.py`, `0x0BFA0700`, 3×32-bit,
  big-endian, 24-hex), gespeichert in `provision-<uid>.json` — **nicht** zur Laufzeit.

## Ziel-Contract (neu)

```json
{
  "schema": 2,
  "module": "fm",
  "identity": {
    "type": "fm_transceiver",
    "model": "SA818-V",
    "version": "26.07.04-01",
    "variant": "vhf",
    "uid": "A1B2C3D4E5F6789AABBCCDDE",
    "uid_source": "stm32_uid"
  },
  "capabilities": [ ... ]
}
```

- `type` unverändert (`fm_transceiver`) — bleibt der Kompatibilitäts-Anker.
- `model` unverändert (`SA818-V`/`SA818-U`).
- **`version`** = FW-Version aus `APP_VERSION` (CalVer `YY.MM.DD-NN`; Dev-Build
  entsprechend `app/VERSION`).
- **`variant`** = `vhf`/`uhf` (aus DT-Band).
- **`uid`** = opaker String; real = STM32-UID (24-hex), sim = synthetisch.
- **`uid_source`** = `stm32_uid` (real) / `synthetic` (native_sim).
- `schema` von 1 → **2** anheben (additive Feld-Erweiterung + `version`-Semantikwechsel;
  der Schema-Bump macht den Contract-Wechsel für Konsumenten explizit).

> **Kompatibilität:** Der Agent (station-manager `slot_discovery`) reicht `identity`
> unverändert durch; A ingestet `uid`/`uid_source` und nutzt `version` als
> `last_reported_version`. Die Umstellung `version`=Band→FW-Version ist genau der Fix,
> den A/C brauchen. Bestehende FW-interne Konsumenten/Tests, die auf `version`==Band
> geprüft haben, werden auf `variant` umgestellt.

## Implementierungs-Bereiche

### 1. `identity`-Struct erweitern (`include/oe5xrx/module/iface.h`)
`struct Identity { type; model; version; }` → zusätzlich `variant`, `uid`,
`uid_source`. `version`/`uid` sind nun **Laufzeitwerte** (nicht rein `const char*`
Compile-Time) — Ablage-Strategie (statischer Buffer, beim ersten Describe/Boot befüllt)
im Plan festlegen. JSON-Serialisierung des Describe-Handlers entsprechend erweitern.

### 2. Laufzeit-UID (fm_board) — `hwinfo`
- `CONFIG_HWINFO=y`; `hwinfo_get_device_id()` liefert die STM32-UID-Bytes.
- **Formatter**, der exakt `read_uid.py::format_uid` spiegelt (big-endian, High-Word
  zuerst, 24-hex, zero-padded). Idealerweise als klar getestete Einheit, mit einem
  Test, der gegen denselben Vektor prüft wie `scripts/tests` (Cross-Check zu
  `read_uid.py`).
- Fail-safe: falls `hwinfo` fehlschlägt, definierter Fallback (z.B. leere UID +
  Log-Warnung) statt Crash — Detail im Plan.

### 3. native_sim — synthetische persistierte UID
- Beim ersten Start eine **stabile** synthetische UID erzeugen und im schreibbaren
  Bereich persistieren (über Neustarts stabil), `uid_source=synthetic`.
- Persistenz-Mechanismus (Datei im Sim-Arbeitsverzeichnis bzw. Zephyr-`settings`)
  im Plan festlegen. Format darf „uid-artig" sein (A behandelt `uid` als opak).

### 4. FW-Version + Variante
- `version` aus `APP_VERSION_*` (dieselbe Quelle wie `version_shell.cpp`), Format
  identisch zum `APP-VERSION`-Output (`YY.MM.DD-NN`).
- `variant` aus dem DT-Band-Enum (0=vhf,1=uhf) → String `vhf`/`uhf`.

### 5. Release-Assets auf `vhf`/`uhf` umbenennen
- `release/release-targets.yaml`: `name: fm-sa818-2m`→`fm-sa818-vhf`,
  `fm-sa818-70cm`→`fm-sa818-uhf`.
- Alle Referenzen mitziehen: `.github/workflows/release.yml`, Doku, `provision.sh`-
  Beispiele/`--version`-Hinweise. Grep nach `2m`/`70cm` im Repo.
- Signatur/cosign-Pfade folgen den neuen Namen automatisch (Namen sind abgeleitet).

## UID-Format-Konsistenz (kritisch)

Der einzige „harte" Korrektheitspunkt: Laufzeit-UID (FW) **==** Provisioning-UID
(`read_uid.py`). Beide lesen dieselbe STM32-UID, müssen aber denselben String
erzeugen. Der Plan **muss** einen Cross-Check-Test enthalten (gleicher 3-Wort-
Eingabevektor → gleicher 24-hex-String in FW-Formatter und `read_uid.py::format_uid`).
Regression hier = A kann Module nicht identifizieren.

## Repo-übergreifende Wirkung

- **A (station-manager):** ingestet künftig echte `uid`/`uid_source` (statt Graceful-
  No-UID) und echte `version` (statt Band). Kein Codewechsel in A nötig — A wurde
  bereits so gebaut; real-HW-UID wird damit „scharf".
- **B:** `ModuleFirmwareRelease.variant` nutzt `vhf`/`uhf` (identisch zu Asset-Namen
  und Descriptor).
- **C:** Ist/Soll-Vergleich auf `identity.version` (jetzt echte FW-Version) +
  Varianten-Match über `variant`.

## Testing

- `tests/sim_shell/pytest/test_module_iface.py`: `identity`-Assertions aktualisieren
  (`version` = FW-Version, neues `variant`, `uid` vorhanden + `uid_source=synthetic`
  im native_sim). Persistenz der synthetischen UID über Neustart prüfen.
- UID-Formatter-Unit-Test (Cross-Check zu `read_uid.py::format_uid`, gleicher Vektor).
- Release-Namens-Änderung: falls Tests/Checks Asset-Namen prüfen, mitziehen; ein
  Dry-Run-Release (`workflow_dispatch dry_run=true`) sollte die neuen Namen zeigen.
- `schema`-Bump: sicherstellen, dass kein Konsument hart auf `schema==1` prüft.

## Offene Implementierungspunkte (kein Architektur-Risiko)

1. Ablage der Laufzeit-Strings (`uid`/`version`) im `Identity` (statischer Buffer vs.
   Lazy-Init beim ersten Describe).
2. Persistenz-Mechanismus der synthetischen native_sim-UID (Datei vs. `settings`).
3. Genaue Fallback-Semantik bei `hwinfo`-Fehler.

## Übergabe an die Umsetzung

Nach Freigabe: `writing-plans` → Plan auf diesem Branch
(`feature/identity-descriptor-uid`). Umsetzung als Kontor-Kind-Session (Manager-Muster):
eigener git-worktree, TDD (sim_shell/pytest + Formatter-Test), PR → CI grün →
Copilot-Loop bis 0, dann „done". Merge über die GitHub-API. **Embedded-Domäne**
(Zephyr/C++): ausführende Agenten entsprechend (rhythm/link/volt + forge fürs Build).
