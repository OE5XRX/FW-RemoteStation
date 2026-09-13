# Design: FW-Produktions-Signing-Key & Provisioning (Teilbereich E)

**Datum:** 2026-09-12
**Status:** Implementiert (PR #64) — Bench-Validierung (Task 7) ausstehend
**Repo:** `FW-RemoteStation` (Release-Workflow, sysbuild-Config, Provisioning-Skript)
**Bezug:**
- Teilbereich **E** des übergeordneten Vorhabens *Modul-Firmware-Update* (Overview-Spec:
  `station-manager/docs/superpowers/specs/2026-09-12-module-firmware-update-design.md`).
- Baut auf `2026-07-16-mcuboot-dfu-secure-bringup-design.md` (On-Device-Trust-Wurzel,
  ECDSA-P256, swap-using-offset, Health-Gate — alles **locked**) und
  `2026-07-04-fw-release-process-design.md` (release-targets.yaml, cosign, `YY.MM.DD-NN`).

---

## 1. Ziel & Kontext

Der Release-Workflow **baut die signierten DFU-Artefakte bereits** (`*.signed.bin` +
`*.mcuboot.hex` für beide Targets, cosign-keyless-signiert, SHA256SUMS). Er signiert aber
noch mit **MCUboots eingebautem Development-Key**. Das steht wörtlich im Code:

- `app/sysbuild.conf`: *„M0/M1 bringup: MCUboot's insecure development key … Replaced with a
  project key in Task 8. DO NOT SHIP THIS."*
- `.github/workflows/release.yml`: *„Signed with MCUboot's built-in DEV key for now … Switch
  to the OE5XRX key later by materializing it from a secret and adding
  `-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=…`"*

**E schließt genau diese Lücke** — der Wechsel auf einen echten, geheimen Produktions-Key —
und ergänzt das Minimum drumherum, damit der Trust wirklich trägt:

1. Produktions-Schlüsselpaar erzeugen (**auf der Maschine des Betreibers, nie auf dem Server /
   nie in einer Agent-Session**).
2. Sichere Ablage: Private als GitHub-**Org-Secret** + Private **und** Public im
   **Passwort-Manager** (Offline-Backup).
3. Release-Workflow verdrahten: Key aus Secret materialisieren, in den `--sysbuild`-Build
   einspeisen.
4. **Verify-Guard**: der Release schlägt fehl, wenn ein Artefakt *nicht* produktions-signiert
   ist (Anti-„ups, dev-signiert ausgeliefert").
5. **Provisioning-Flow** inkl. **STM32-UID-Auslesen** (der E↔A-Berührungspunkt).

## 2. Getroffene Entscheidungen (locked)

| Frage | Entscheidung | Begründung |
|---|---|---|
| Key-Herkunft | **Betreiber erzeugt lokal**, Agent/Server sieht den Private Key **nie** | Trust-Wurzel darf keine Server-Kompromittierung überleben |
| Key-Ablage (aktiv) | GitHub **Org-Secret** `MCUBOOT_SIGNING_KEY_ECDSA_P256` (volle PEM) | Alle HW-Modul-Release-Workflows teilen ihn; nur CI braucht ihn |
| Key-Ablage (Backup) | Private **und** Public im **Team-Passwort-Manager** | Org-Secrets sind nicht rücklesbar; Verlust = SWD-Recovery jedes Geräts |
| Key-Datei im Build | **nur** per `-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=` auf der CLI (Release) | Key nie im Repo; `keys/` bleibt gitignored |
| `sysbuild.conf` | behält **Dev-Key-Default** (Key-File-Zeile bleibt ungesetzt) | Lokale + CI-Nicht-Release-Builds bleiben reproduzierbar, kein Secret nötig |
| Prod-Signatur nur im | echten Release (`workflow_dispatch`, non-dry-run, Default-Branch) | Dry-run/CI = dev-signiert, real = prod-signiert |
| Verify-Guard | `imgtool verify` gegen den Public Key, **fail** wenn nicht prod-signiert | Verhindert versehentliche Dev-Key-Auslieferung |
| Provisioning | `.mcuboot.hex` (prod-pubkey) + erste `*.signed.bin` per **SWD/pyocd** | Bootloader ist nicht feld-updatebar (siehe Bringup-Spec) |
| UID-Erfassung | Provisioning-Skript **liest + gibt aus** (stdout/Datei); Registrierung = A | E bleibt self-contained im FW-Repo |
| Cutover | **sauber** — genau **ein** Board im Lab, keine Feld-Boards | Kein Migrations-/Re-Provisioning-Aufwand |

## 3. Schlüssel-Verwaltung — Key-Zeremonie (Runbook)

> **Wichtig:** Alle Schritte hier führt der **Betreiber lokal** aus. Der geheime Key darf
> **nicht** über die Agent-Session / den Server laufen. Der Agent begleitet nur anleitend.

**Voraussetzung:** `imgtool` lokal verfügbar (`python -m pip install imgtool`).

1. **Schlüsselpaar erzeugen** (ECDSA-P256, wie in der Bringup-Spec gelockt):
   ```bash
   imgtool keygen -k oe5xrx-fw-ecdsa-p256.pem -t ecdsa-p256
   ```
   → `oe5xrx-fw-ecdsa-p256.pem` enthält Private **und** ableitbaren Public Key. Diese eine
   Datei treibt im sysbuild-Build sowohl das **Signieren** der App als auch den **in MCUboot
   verbackenen Public Key** — beide bleiben damit konsistent.

2. **Public Key extrahieren** (für Verify-Guard / Doku):
   ```bash
   imgtool getpub -k oe5xrx-fw-ecdsa-p256.pem            # C-Array
   imgtool getpub -k oe5xrx-fw-ecdsa-p256.pem -e pem     # PEM
   ```

3. **Ablegen:**
   - **GitHub-Org-Secret** `MCUBOOT_SIGNING_KEY_ECDSA_P256` = **voller PEM-Inhalt** des Private
     Key (Org-Ebene `OE5XRX`, damit alle HW-Modul-Workflows ihn teilen).
   - **Passwort-Manager:** Private **und** Public PEM als sicheres Offline-Backup.

4. **Nie committen:** `keys/` bleibt in `.gitignore`; die lokale PEM nach dem Hinterlegen
   löschen oder offline verwahren.

> **Kritisch:** Org-Secrets sind nicht rücklesbar. Ohne das Passwort-Manager-Backup und bei
> Verlust des Keys kann **niemand** mehr Firmware signieren, die bereits provisionierte
> Bootloader akzeptieren → Recovery nur per physischem SWD an **jedem** Gerät. Key = langlebig.

## 4. Release-Workflow-Änderungen (`.github/workflows/release.yml`)

Minimal-invasiv, nur der Prod-Build-Zweig:

1. **Key materialisieren** (nur bei non-dry-run) — vor dem Build:
   ```bash
   umask 077
   printf '%s' "${{ secrets.MCUBOOT_SIGNING_KEY_ECDSA_P256 }}" > "$RUNNER_TEMP/oe5xrx-fw.pem"
   ```
2. **Sysbuild-Build mit Key** — den bestehenden `build-signed`-Aufruf ergänzen:
   ```bash
   west build -b "$board" --sysbuild app -p always -d build-signed -- \
     -Dapp_EXTRA_DTC_OVERLAY_FILE="$overlay" \
     -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE="$RUNNER_TEMP/oe5xrx-fw.pem"
   ```
   Bei **dry-run** wird der Key-Parameter weggelassen → dev-signiert (reproduzierbar, kein
   Secret nötig). `sysbuild.conf` bleibt unangetastet (Dev-Key-Default).
3. **Temp-Key am Ende schreddern** (`shred`/`rm`), auch bei Fehlschlag (`if: always()`).
4. **`sysbuild.conf`-Kommentar** aktualisieren: Dev-Key ist nur noch der lokale/CI-Default;
   den „Task 8"-Hinweis auf „siehe Release-Workflow / diese Spec" umschreiben.

## 5. Verify-Guard

Neuer Release-Step nach dem Build, vor dem Publish:
```bash
imgtool verify -k <prod-public.pem> "release/out/${name}.signed.bin"
```
- non-dry-run: schlägt fehl, wenn das Artefakt **nicht** mit dem Prod-Key signiert ist →
  Release bricht ab.
- Öffnet die Frage: woher der Public Key im Guard? Optionen: (a) aus dem materialisierten
  Private Key ableiten (`imgtool getpub`), (b) den Public Key committen (nicht geheim) und
  gegen ihn prüfen. **Empfehlung (b)** — der committete Public Key ist zugleich
  nachprüfbare Doku dessen, was die Feld-Bootloader trusten. (Entscheidung im Plan finalisieren.)

## 6. Provisioning-Flow + UID-Auslesen (E↔A)

Ein Skript (z. B. `scripts/provision.sh`) für das **einmalige Bench-Provisioning** pro Board:
1. Bootloader + erste signierte App per SWD flashen (wie Bringup-Spec):
   ```bash
   pyocd flash --target stm32u575citx --erase chip "<name>.mcuboot.hex"
   pyocd flash --target stm32u575citx --base-address 0x08020000 "<name>.signed.bin"
   ```
2. **STM32-UID auslesen** (96-bit Unique Device ID) via pyocd (`pyocd commander` / Register-
   Read am UID-Basisregister) und **ausgeben** (stdout + `provision-<uid>.json` mit
   `uid`, `module_type`, geflashte `version`).
3. **Ende der E-Verantwortung.** Die Registrierung dieser UID in den station-manager ist
   **Teilbereich A** — E liefert nur die Daten.

> Offener Punkt (Plan): exakte UID-Leseform mit pyocd auf dem U575 + Ziel-Kürzel `stm32u575xi`
> gegen die reale Chip-Variante verifizieren.

## 7. Dev/Prod-Koexistenz & Cutover

- **Inkompatibilität by design:** ein mit Prod-Bootloader provisioniertes Board akzeptiert
  **nur** prod-signierte Images; dev-signierte booten dort nicht (und umgekehrt). Das ist die
  Sicherheit, die funktioniert.
- **Lokale/CI-Builds** bleiben dev-signiert (kein Secret, reproduzierbar). Nur der echte
  Release ist prod-signiert.
- **Cutover trivial:** genau ein Board im Lab. Einmal mit Prod-Bootloader neu provisionieren,
  fertig. Keine Feld-Boards, kein Massen-Reflash.

## 8. Betroffene Files

- `.github/workflows/release.yml` — Key materialisieren, `-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE`,
  Verify-Guard, Temp-Key-Shred.
- `app/sysbuild.conf` — Kommentar aktualisieren (Dev-Key = nur lokal/CI-Default).
- `scripts/provision.sh` (neu) — SWD-Flash + UID-Auslesen.
- `keys/.gitignore` bzw. `.gitignore` — sicherstellen, dass `keys/` / `*.pem` nie eingecheckt
  werden.
- ggf. `docs/firmware-update.md` — Prod-Key-Realität nachziehen.
- Public-Key-Datei (committed, nicht geheim) für den Verify-Guard — Ort im Plan festlegen.

## 9. Scope / YAGNI

**Nicht in E:**
- Bootloader-Recovery (M4 der Bringup-Spec), RDP/WRP/TrustZone/Anti-Tamper (Tier B).
- Automatisierte Key-Rotation (Key ist bewusst langlebig).
- Die station-manager-Registrierungs-API für UIDs (= Teilbereich A).
- Der Agent-seitige DFU-Flash-Ablauf (= Teilbereich D).

## 10. Test & Verifikation

- **Dry-run-Release** baut weiterhin dev-signiert durch (kein Secret nötig).
- **Non-dry-run** (Test-Tag) erzeugt prod-signierte `*.signed.bin`; **Verify-Guard grün**.
- Absichtlich dev-signiertes Artefakt → **Verify-Guard rot** (Guard greift).
- **Bench:** das eine Lab-Board mit Prod-Bootloader provisionieren; UID wird ausgelesen +
  ausgegeben.
- **DFU-Update** mit prod-signiertem Image auf dem prod-provisionierten Board → bootet, meldet
  neue Version. Ein **dev-signiertes** Image → wird abgewiesen (Trust-Wurzel verifiziert).

## 11. Offene Punkte

1. **Bench-Validierung (Task 7):** exakte pyocd-UID-Leseform + Chip-Target auf realer
   Chip-Variante bestätigen (`0x0BFA0700` gegen RM0456), plus DFU-Gegenprobe.

Die übrigen ursprünglichen Punkte (committeter Public-Key-Pfad + Verify-Guard, `provision.sh`-
JSON-Schema) sind **implementiert** (Abschnitte 5–6) und damit geschlossen.
