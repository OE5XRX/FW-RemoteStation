# FW-Produktions-Signing-Key & Provisioning (Teilbereich E) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Den FW-Release von MCUboots Development-Key auf einen echten, geheimen OE5XRX-Produktions-Key umstellen und ein Bench-Provisioning-Skript liefern, das die STM32-UID ausliest.

**Architecture:** Der Release-Workflow baut die signierten `--sysbuild`-Artefakte bereits; wir speisen bei echten Releases den Produktions-Key aus einem Org-Secret ein, verifizieren das Ergebnis (Guard) und schreddern den Temp-Key. `sysbuild.conf` behält den Dev-Key-Default für lokale/CI-Builds. Ein neues `provision.sh` flasht Bootloader + erste signierte App per SWD und liest die 96-bit-UID aus (Übergabe an Teilbereich A). Der geheime Key wird **ausschließlich vom Betreiber lokal** erzeugt — nie in dieser Session/auf dem Server.

**Tech Stack:** GitHub Actions (`workflow_dispatch`), Zephyr sysbuild + MCUboot, `imgtool` (keygen/getpub/verify/sign), `pyocd` (SWD-Flash + Memory-Read), bash, `yq`/`actionlint`/`shellcheck`.

**Spec:** `docs/superpowers/specs/2026-09-12-fw-production-signing-key-design.md`

## Global Constraints

- **Signature-Algo:** ECDSA-P256 (locked in `2026-07-16-mcuboot-dfu-secure-bringup-design.md`) — nicht ändern.
- **Der geheime Private Key darf niemals** ins Repo, in Logs, in diese Agent-Session oder auf den Server. Ablage: GitHub-Org-Secret `MCUBOOT_SIGNING_KEY_ECDSA_P256` + Team-Passwort-Manager.
- **Key-Datei nur per CLI** (`-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=`) im Release; `app/sysbuild.conf` bleibt beim Dev-Key-Default (Key-File-Zeile ungesetzt).
- **Prod-Signatur nur** bei non-dry-run auf dem Default-Branch. Dry-run/CI = dev-signiert.
- **pyocd-Target:** `stm32u575citx` (fm_board = STM32U575CIT); auf realer Chip-Variante am Bench bestätigen.
- **Branch:** `feat/fw-production-signing`.

## Entscheidungen — die drei offenen Punkte der Spec, hier geschlossen

1. **Verify-Guard-Public-Key: committed** (Variante b). Der Public Key liegt (nicht geheim) unter `release/signing/oe5xrx-fw-public.pem` und ist zugleich nachprüfbare Doku dessen, was Feld-Bootloader trusten. `.gitignore` blockt nur den **Private** Key.
2. **UID-Lesen:** über die **pyocd-Python-API**, STM32U5 Unique-ID-Basis **`0x0BFA0700`** (3×32-bit), formatiert als Big-Endian-Hex-String. Exakte Adresse am Bench gegen RM0456 bestätigen.
3. **provision.sh-Ausgabe:** `provision-<uid>.json` mit Schema `{uid, module_type, pyocd_target, firmware_version, signed_app, mcuboot_hex, provisioned_at}` — genau die Felder, die Teilbereich A zur Registrierung erwartet.

## Task-Übersicht & Ausführungsart

| Task | Deliverable | Ausführung |
|---|---|---|
| 1 | `.gitignore`-Schutz gegen Key-Commits | subagent |
| 2 | Release-Workflow: Key materialisieren + einspeisen + shred + sysbuild-Kommentar | subagent |
| 3 | Verify-Guard im Release-Workflow | subagent |
| 4 | `scripts/read_uid.py` (UID-Format-Logik, unit-getestet) | subagent |
| 5 | `scripts/provision.sh` (SWD-Flash + UID-Read + JSON) | subagent (Syntax/shellcheck); HIL am Bench durch Betreiber |
| 6 | **Key-Zeremonie** (Key erzeugen, Org-Secret, Public committen) | **Betreiber interaktiv** (Agent leitet an) |
| 7 | **Bench-Validierung** am Lab-Board | **Betreiber (HIL)** |

---

### Task 1: Private Keys vor versehentlichem Commit schützen

**Files:**
- Modify: `.gitignore`

**Interfaces:**
- Produces: garantiert, dass `keys/` und private PEMs nie eingecheckt werden; der committete Public Key (`release/signing/oe5xrx-fw-public.pem`) bleibt erlaubt.

- [ ] **Step 1: `.gitignore` ergänzen**

Am Ende von `.gitignore` anhängen:

```gitignore
# FW signing keys — NEVER commit the private key (Teilbereich E)
keys/
*-ecdsa-p256.pem
oe5xrx-fw*.pem
!release/signing/oe5xrx-fw-public.pem
```

- [ ] **Step 2: Verifizieren, dass der Public-Key-Pfad nicht ignoriert wird**

Run:
```bash
mkdir -p release/signing && touch release/signing/oe5xrx-fw-public.pem keys/oe5xrx-fw-ecdsa-p256.pem
git check-ignore -v keys/oe5xrx-fw-ecdsa-p256.pem release/signing/oe5xrx-fw-public.pem || true
```
Expected: `keys/oe5xrx-fw-ecdsa-p256.pem` wird ignoriert (Zeile matcht), `release/signing/oe5xrx-fw-public.pem` **nicht** (kein Output für diese Datei). Danach nur den **untracked** Platzhalter entfernen: `rm -rf keys` — die committete `release/signing/oe5xrx-fw-public.pem` ist getrackt und **darf nicht** gelöscht werden.

- [ ] **Step 3: Commit**

```bash
git add .gitignore
git commit -m "chore: gitignore FW signing private keys (Teilbereich E)"
```

---

### Task 2: Release-Workflow — Produktions-Key materialisieren & einspeisen

**Files:**
- Modify: `.github/workflows/release.yml` (Step „Build all targets", Prod-Build-Zweig; neuer Materialisier-Step davor; Shred-Step danach)
- Modify: `app/sysbuild.conf` (Kommentar)

**Interfaces:**
- Consumes: Org-Secret `MCUBOOT_SIGNING_KEY_ECDSA_P256` (voller Private-PEM-Inhalt; wird in Task 6 gesetzt).
- Produces: bei non-dry-run prod-signierte `${name}.signed.bin`; Temp-Key unter `$RUNNER_TEMP/oe5xrx-fw.pem` (nach Build geschreddert).

- [ ] **Step 1: Materialisier-Step einfügen** (vor „Build all targets")

```yaml
      - name: Materialize production signing key
        if: ${{ !inputs.dry_run }}
        env:
          SIGNING_KEY: ${{ secrets.MCUBOOT_SIGNING_KEY_ECDSA_P256 }}
        run: |
          set -euo pipefail
          if [ -z "${SIGNING_KEY}" ]; then
            echo "::error::MCUBOOT_SIGNING_KEY_ECDSA_P256 secret is empty — cannot sign a real release." >&2
            exit 1
          fi
          umask 077
          printf '%s' "${SIGNING_KEY}" > "$RUNNER_TEMP/oe5xrx-fw.pem"
          echo "SIGNING_KEY_FILE=$RUNNER_TEMP/oe5xrx-fw.pem" >> "$GITHUB_ENV"
```

- [ ] **Step 2: Prod-Build-Aufruf um den Key-Parameter ergänzen**

Im „Build all targets"-Step den `build-signed`-Aufruf ändern von:

```bash
                west build -b "$board" --sysbuild app -p always -d build-signed -- \
                  -Dapp_EXTRA_DTC_OVERLAY_FILE="$overlay"
```

zu (Key nur wenn gesetzt → dry-run bleibt dev-signiert):

```bash
                keyarg=""
                if [ -n "${SIGNING_KEY_FILE:-}" ]; then
                  keyarg="-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=${SIGNING_KEY_FILE}"
                fi
                west build -b "$board" --sysbuild app -p always -d build-signed -- \
                  -Dapp_EXTRA_DTC_OVERLAY_FILE="$overlay" $keyarg
```

- [ ] **Step 3: Shred-Step nach „Build all targets" einfügen**

```yaml
      - name: Shred signing key
        if: ${{ always() }}
        run: |
          set -euo pipefail
          if [ -n "${SIGNING_KEY_FILE:-}" ] && [ -f "${SIGNING_KEY_FILE}" ]; then
            shred -u "${SIGNING_KEY_FILE}" || rm -f "${SIGNING_KEY_FILE}"
          fi
```

- [ ] **Step 4: `app/sysbuild.conf`-Kommentar aktualisieren**

Ersetze den Kommentarblock:

```
# M0/M1 bringup: MCUboot's insecure development key (root-ec-p256.pem in mcuboot tree).
# Replaced with a project key in Task 8. DO NOT SHIP THIS.
```

durch:

```
# Local + CI builds use MCUboot's built-in development key (root-ec-p256.pem in
# the mcuboot tree) — reproducible, not secret. Real releases override this with
# the OE5XRX production key via the CLI (-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE),
# materialized in .github/workflows/release.yml from the org secret. Never set a
# key file path here (would risk committing the key). See
# docs/superpowers/specs/2026-09-12-fw-production-signing-key-design.md.
```

- [ ] **Step 5: Workflow-Syntax prüfen**

Run:
```bash
actionlint .github/workflows/release.yml || pipx run actionlint .github/workflows/release.yml
yq '.jobs.release.steps | length' .github/workflows/release.yml
```
Expected: actionlint ohne Fehler; yq gibt eine Zahl (Steps parsebar).

- [ ] **Step 6: Commit**

```bash
git add .github/workflows/release.yml app/sysbuild.conf
git commit -m "feat: sign real releases with the OE5XRX production key (Teilbereich E)"
```

---

### Task 3: Verify-Guard — kein Release ohne Prod-Signatur

**Files:**
- Modify: `.github/workflows/release.yml` (neuer Step nach „Build all targets", vor „Generate SHA256SUMS")

**Interfaces:**
- Consumes: `release/signing/oe5xrx-fw-public.pem` (committed in Task 6), die gebauten `release/out/*.signed.bin`.
- Produces: Abbruch des non-dry-run-Release, falls ein `*.signed.bin` **nicht** gegen den Prod-Public-Key verifiziert.

- [ ] **Step 1: Guard-Step einfügen**

```yaml
      - name: Verify artifacts are production-signed
        if: ${{ !inputs.dry_run }}
        working-directory: fw
        run: |
          set -euo pipefail
          pub="release/signing/oe5xrx-fw-public.pem"
          if [ ! -f "$pub" ]; then
            echo "::error::$pub missing — commit the production public key (see key ceremony) before a real release." >&2
            exit 1
          fi
          shopt -s nullglob
          imgs=( release/out/*.signed.bin )
          if [ ${#imgs[@]} -eq 0 ]; then
            echo "::error::no signed images to verify" >&2
            exit 1
          fi
          for img in "${imgs[@]}"; do
            echo "verify: $img"
            imgtool verify -k "$pub" "$img"
          done
          echo "All signed images verified against the production key."
```

- [ ] **Step 2: Guard-Logik lokal gegen den Dev-Key gegenprüfen** (Mechanismus-Test ohne Prod-Key)

Dieser Test beweist, dass `imgtool verify` einen *falschen* Key ablehnt — mit den vorhandenen Dev-Key-Artefakten, ohne den Prod-Key zu brauchen.

Run:
```bash
cd fw 2>/dev/null || true
python -m pip install --break-system-packages imgtool >/dev/null 2>&1 || true
# Dev-key public aus dem mcuboot-Tree holen und ein Wegwerf-Fremdkey erzeugen:
imgtool keygen -k /tmp/other.pem -t ecdsa-p256
imgtool getpub -k /tmp/other.pem -e pem > /tmp/other-pub.pem
# Ein dev-signiertes Image bauen (falls noch keins vorliegt) und prüfen:
west build -b fm_board --sysbuild app -p always -d /tmp/build-dev -- \
  -Dapp_EXTRA_DTC_OVERLAY_FILE="$PWD/release/overlays/band_vhf.overlay"
imgtool verify -k /tmp/other-pub.pem /tmp/build-dev/app/zephyr/zephyr.signed.bin; echo "exit=$?"
```
Expected: `imgtool verify` gegen den **fremden** Key schlägt fehl (exit != 0) → der Guard würde einen falsch/fremd signierten Release ablehnen. (Aufräumen: `rm -rf /tmp/build-dev /tmp/other*.pem`.)

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "feat: fail release when artifacts are not production-signed (Teilbereich E)"
```

---

### Task 4: `read_uid.py` — UID-Formatierung (unit-getestet)

**Files:**
- Create: `scripts/read_uid.py`
- Test: `scripts/tests/test_read_uid.py`

**Interfaces:**
- Produces: `format_uid(words: list[int]) -> str` — nimmt drei 32-bit-Wörter (wie von `read32` an aufsteigenden Adressen gelesen) und liefert einen stabilen, kanonischen Hex-String (Big-Endian, höchstes Wort zuerst, 24 Hex-Zeichen). Wird von `provision.sh` genutzt; die kanonische UID-Form ist zugleich die Registrierungs-ID für Teilbereich A.

- [ ] **Step 1: Failing test schreiben**

`scripts/tests/test_read_uid.py`:
```python
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from read_uid import format_uid

def test_format_uid_big_endian_high_word_first():
    # words read at 0x..0700, 0x..0704, 0x..0708 (ascending) -> high word first
    assert format_uid([0x11223344, 0x55667788, 0x99AABBCC]) == "99AABBCC5566778811223344"

def test_format_uid_zero_padding():
    assert format_uid([0x1, 0x0, 0x0]) == "000000000000000000000001"

def test_format_uid_requires_three_words():
    try:
        format_uid([0x1, 0x2])
    except ValueError:
        return
    assert False, "expected ValueError for != 3 words"
```

- [ ] **Step 2: Test laufen lassen (muss fehlschlagen)**

Run: `python -m pytest scripts/tests/test_read_uid.py -v`
Expected: FAIL (`read_uid` bzw. `format_uid` existiert nicht).

- [ ] **Step 3: Implementieren**

`scripts/read_uid.py`:
```python
#!/usr/bin/env python3
"""Read the STM32U5 96-bit unique device ID over SWD via pyocd, or format words.

format_uid() is pure and unit-tested; read_uid_from_target() needs hardware.
"""
from __future__ import annotations

STM32U5_UID_BASE = 0x0BFA0700  # RM0456 — confirm on real silicon at the bench


def format_uid(words: list[int]) -> str:
    """Three 32-bit words (read at ascending addresses) -> canonical hex string.

    High word first (big-endian), zero-padded to 24 hex chars.
    """
    if len(words) != 3:
        raise ValueError(f"expected 3 words, got {len(words)}")
    return "".join(f"{w & 0xFFFFFFFF:08X}" for w in reversed(words))


def read_uid_from_target(target: str = "stm32u575citx") -> str:  # pragma: no cover
    """Connect via pyocd and read the UID. Requires an attached probe + board."""
    from pyocd.core.helpers import ConnectHelper

    with ConnectHelper.session_with_chosen_probe(target_override=target) as session:
        t = session.target
        words = [t.read32(STM32U5_UID_BASE + i * 4) for i in range(3)]
    return format_uid(words)


if __name__ == "__main__":  # pragma: no cover
    import argparse

    ap = argparse.ArgumentParser()
    ap.add_argument("--target", default="stm32u575citx")
    print(read_uid_from_target(ap.parse_args().target))
```

- [ ] **Step 4: Test laufen lassen (muss bestehen)**

Run: `python -m pytest scripts/tests/test_read_uid.py -v`
Expected: PASS (3 Tests).

- [ ] **Step 5: Commit**

```bash
git add scripts/read_uid.py scripts/tests/test_read_uid.py
git commit -m "feat: STM32U5 UID formatting helper with tests (Teilbereich E)"
```

---

### Task 5: `provision.sh` — Bench-Provisioning + UID-Report

> **Historisch (nicht die Wahrheit über den Auslieferungsstand):** Der unten
> gezeigte `provision.sh`-Codeblock ist der ursprüngliche Plan-Entwurf. Das real
> ausgelieferte Skript wurde im Code-Review weiterentwickelt (fail-closed
> Signatur-Guard vor dem Erase, JSON via Encoder, `connect_mode=under-reset`,
> pyocd-Interpreter für `read_uid.py`). **Maßgeblich ist `scripts/provision.sh` im
> Repo**, nicht dieser Entwurf.


**Files:**
- Create: `scripts/provision.sh`

**Interfaces:**
- Consumes: `read_uid.py` (`read_uid_from_target`), veröffentlichte Assets `${name}.mcuboot.hex` + `${name}.signed.bin`.
- Produces: geflashtes Board (Prod-Bootloader + erste signierte App) und `provision-<uid>.json` (Schema aus „Entscheidungen") — Eingabe für Teilbereich A.

- [ ] **Step 1: Skript schreiben**

`scripts/provision.sh`:
```bash
#!/usr/bin/env bash
# One-time bench provisioning of an fm_board: flash the production MCUboot +
# first signed app over SWD, then read the STM32 UID and emit provision-<uid>.json.
# HARDWARE REQUIRED: an ST-Link/CMSIS-DAP probe + the target board attached.
set -euo pipefail

usage() {
  echo "usage: $0 --mcuboot <mcuboot.hex> --app <signed.bin> --type <module_type> --version <YY.MM.DD-NN> [--target <pyocd_target>] [--slot0 <addr>]" >&2
  exit 2
}

TARGET="stm32u575citx"
SLOT0="0x08020000"   # slot0 base (see mcuboot-dfu-secure-bringup spec / fm_board.dts)
MCUBOOT="" APP="" MTYPE="" VERSION=""
while [ $# -gt 0 ]; do
  case "$1" in
    --mcuboot) MCUBOOT="$2"; shift 2;;
    --app) APP="$2"; shift 2;;
    --type) MTYPE="$2"; shift 2;;
    --version) VERSION="$2"; shift 2;;
    --target) TARGET="$2"; shift 2;;
    --slot0) SLOT0="$2"; shift 2;;
    *) usage;;
  esac
done
[ -n "$MCUBOOT" ] && [ -n "$APP" ] && [ -n "$MTYPE" ] && [ -n "$VERSION" ] || usage
[ -f "$MCUBOOT" ] || { echo "no such file: $MCUBOOT" >&2; exit 1; }
[ -f "$APP" ] || { echo "no such file: $APP" >&2; exit 1; }

echo "== Flashing bootloader ($MCUBOOT) with chip erase =="
pyocd flash --target "$TARGET" --erase chip "$MCUBOOT"

echo "== Flashing signed app ($APP) at $SLOT0 =="
pyocd flash --target "$TARGET" --base-address "$SLOT0" "$APP"

echo "== Reading UID =="
here="$(cd "$(dirname "$0")" && pwd)"
uid="$(python3 "$here/read_uid.py" --target "$TARGET")"
echo "UID=$uid"

out="provision-${uid}.json"
ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
cat > "$out" <<EOF
{
  "uid": "${uid}",
  "module_type": "${MTYPE}",
  "pyocd_target": "${TARGET}",
  "firmware_version": "${VERSION}",
  "signed_app": "$(basename "$APP")",
  "mcuboot_hex": "$(basename "$MCUBOOT")",
  "provisioned_at": "${ts}"
}
EOF
echo "Wrote $out — hand this to station-manager module registration (Teilbereich A)."
```

- [ ] **Step 2: Ausführbar machen + shellcheck**

Run:
```bash
chmod +x scripts/provision.sh
shellcheck scripts/provision.sh
scripts/provision.sh --help 2>&1 | head -1 || true
```
Expected: shellcheck ohne Findings; `usage`-Zeile erscheint bei fehlenden Argumenten (Exit 2).

- [ ] **Step 3: Commit**

```bash
git add scripts/provision.sh
git commit -m "feat: bench provisioning script with UID capture (Teilbereich E)"
```

---

### Task 6: Key-Zeremonie — Betreiber, interaktiv (kein Code)

> **Nicht subagent-fähig.** Der Agent (Manager) leitet den Betreiber Schritt für Schritt an. Der geheime Key wird **lokal** erzeugt und berührt weder Session noch Server.

- [ ] **Step 1: Key erzeugen** (lokal): `imgtool keygen -k oe5xrx-fw-ecdsa-p256.pem -t ecdsa-p256`
- [ ] **Step 2: Public Key extrahieren:** `imgtool getpub -k oe5xrx-fw-ecdsa-p256.pem -e pem > oe5xrx-fw-public.pem`
- [ ] **Step 3: Org-Secret setzen** `MCUBOOT_SIGNING_KEY_ECDSA_P256` = voller Inhalt von `oe5xrx-fw-ecdsa-p256.pem` (GitHub-Org `OE5XRX`, Actions-Secrets).
- [ ] **Step 4: Backup** — Private **und** Public PEM in den Team-Passwort-Manager.
- [ ] **Step 5: Public Key committen:** Datei nach `release/signing/oe5xrx-fw-public.pem` legen, `git add` (von `.gitignore` erlaubt), commit.
- [ ] **Step 6: Lokalen Private Key sicher entfernen** (`shred -u oe5xrx-fw-ecdsa-p256.pem`) oder offline verwahren. **Nie committen.**

---

### Task 7: Bench-Validierung — Betreiber (HIL)

> **Hardware-in-the-Loop** am einen Lab-Board. Der Agent wertet die gemeldeten Beobachtungen aus.

- [ ] **Step 1: Test-Release** (non-dry-run auf Default-Branch, Test-Tag) → Verify-Guard **grün**, prod-signierte Assets.
- [ ] **Step 2: UID-Basisadresse** `0x0BFA0700` gegen RM0456 / reale Chip-Variante bestätigen; falls abweichend, `STM32U5_UID_BASE` in `read_uid.py` + `--target` korrigieren.
- [ ] **Step 3: Provisioning:** `scripts/provision.sh --mcuboot fm-sa818-2m.mcuboot.hex --app fm-sa818-2m.signed.bin --type fm --version <tag>` → Board bootet, `provision-<uid>.json` entsteht, `version`-Shell meldet den Tag.
- [ ] **Step 4: DFU-Gegenprobe:** ein **dev-signiertes** Image auf das prod-provisionierte Board → wird abgewiesen (Trust-Wurzel greift); ein **prod-signiertes** → swap + boot ok.
- [ ] **Step 5: Ergebnis an den Manager melden** (done/blocked/failed) für die Review-Phase.

---

## Self-Review

- **Spec-Coverage:** Key-Zeremonie/Ablage → Task 6 + Global Constraints; Release-Verdrahtung → Task 2; Verify-Guard → Task 3; Provisioning + UID → Task 4/5; sysbuild-Kommentar → Task 2; Dev/Prod-Koexistenz + Cutover → Task 2 (dry-run-Zweig) + Task 7. Drei offene Punkte → „Entscheidungen". ✓
- **Placeholder-Scan:** keine TBD/TODO; alle Snippets konkret. HIL/Betreiber-Schritte sind bewusst als solche markiert (nicht subagent-fähig), mit exakten Kommandos + erwarteten Beobachtungen. ✓
- **Type/Namens-Konsistenz:** `format_uid` / `read_uid_from_target` / `SIGNING_KEY_FILE` / `release/signing/oe5xrx-fw-public.pem` durchgängig gleich. JSON-Schema in „Entscheidungen" == Task 5-Ausgabe. ✓
