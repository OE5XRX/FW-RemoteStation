# Design: FW-RemoteStation Release-Prozess

**Datum:** 2026-07-04
**Status:** Design (genehmigt, vor Implementierung)
**Repo:** `FW-RemoteStation` (Release-Workflow, Target-Manifest, App-Config, Static-Build, Version-Stamping); Konsum-Seite in `linux-image` (Pin-Recipes + Scripts)
**Bezug:** entblockt D2 (`2026-07-04-module-simulation-layer-design.md`) — die Sim-Schicht braucht ein gepinntes `native_sim`-FM-Artefakt. Richtet sich an der Projekt-Release-Kultur aus (linux-image datumsbasierte Releases, station-agent SRCREV-Pin, HW-Module-CI signierte Releases + `<<VERSION>>`-Injektion, cosign).

---

## 1. Ziel & Kontext

**Problem:** FW-RemoteStation hat **keinen Release-Prozess**. `ci.yml` macht nur clang-format + native_sim-Build + Twister-Tests — **kein Artifact-Upload, kein reales `fm_board`-Build, keine Signierung, keine Tags/Releases**. Damit gibt es kein definiertes Artefakt für (a) die D2-Sim-Schicht (`native_sim`-Binary) und (b) DFU/Flash am Bench (reales U575-Binary).

**Zusatz-Kontext:** FW-RemoteStation ist ein **Monorepo für alle Board-Firmwares** (FM, künftig HF …). Varianten (2m/70cm) werden über den **Devicetree** unterschieden (gleiche PCB, bestückt für SA818-V/-U + passendem Filter). Ein Release muss daher Artefakte **pro Board/Variante** liefern, klar benannt.

## 2. Getroffene Entscheidungen

| Frage | Entscheidung | Begründung |
|---|---|---|
| Scope | **Sim + Real** in einem Release | Beide Konsumenten (D2-Sim, Bench-DFU) an einem Tag versorgt. |
| Board-Abdeckung | **Monorepo, Artefakte pro Board/Variante** benannt | „fm sa818 2m" muss sofort auffindbar sein. |
| Target-Definition | **Deklaratives `release-targets.yaml`** (Ansatz A) | Single-Source-of-Truth; neues Board = ein Eintrag. |
| Versionierung | **`YYYY.MM.DD-NN`**, repo-weit | Identisch zu linux-image (z.B. `2026.04.24-18`). |
| Trigger | **Manueller `workflow_dispatch`-Release-Job** (One-Button) | Erzeugt Tag + schreibt Version-Files + baut + published in einem Schritt. |
| Version-Stamping | **Version-File/Header, vom Release-Job geschrieben** | Firmware meldet ihre Version; Image+FW-Bundle eindeutig zuordenbar. |
| Bundle | **linux-image bündelt real *und* native_sim** | Image + Firmware werden zusammen deployed + getestet. |
| Describe-Fähigkeit | **`CONFIG_MODULE_SA818` in `app/prj.conf`** | „geshipptes = getestetes" Binary. |
| native_sim-Linking | **Statisch** (keine externen Lib-Deps) | Löst die glibc-Kopplung. |
| Signierung | **cosign keyless** + sha256 | Wie linux-image; verifizierbar + pinbar. |
| Konsum | **Pin per URL+sha256** (Recipes + Scripts) | Analog station-agent-SRCREV-Pin; Lockfile-Disziplin. |

## 3. Versionierung, Trigger & Version-Stamping

- Schema **`YYYY.MM.DD-NN`** (Datum + Tages-Sequenz), **eine repo-weite Version** für alle Boards.
- **Trigger: ein manueller `workflow_dispatch`-„Release"-Job** (One-Button). Er:
  1. berechnet das nächste `YYYY.MM.DD-NN`,
  2. **schreibt die Version in ein Version-File/Header** (Build-Zeit-Injektion à la HW-Module-CI `<<VERSION>>`),
  3. baut alle Targets (real + native_sim),
  4. signiert (cosign) + erzeugt `SHA256SUMS`,
  5. erstellt den Git-Tag und veröffentlicht das GitHub-Release.
- **Version-Stamping:** die Firmware trägt ihre Release-Version in einem vom Release-Job geschriebenen File/Header und meldet sie **zur Laufzeit** (im `describe`/über ein `version`-Kommando) → Image+Firmware-Bundle sind eindeutig zuordenbar.
- *Anmerkung:* linux-image `release.yml` läuft heute auf **Tag-Push** (Version = Tag-Name, kein committetes Version-File). Der One-Button-Job hier ist die gewünschte Weiterentwicklung; er kann intern den Tag erzeugen (→ tag-getriggerter Build) oder alles in einem Job tun — Detail in der Implementierung.

## 4. Build-Matrix — `release-targets.yaml` (Ansatz A)

Eine Datei im Repo listet die Release-Ziele; der Workflow iteriert darüber:

```yaml
targets:
  - name: fm-sa818-2m
    board: fm_board
    band: vhf                 # DT-`band`-Property → SA818-V, 134–174 MHz
    artifacts: [firmware, native_sim]
  - name: fm-sa818-70cm
    board: fm_board
    band: uhf                 # DT-`band`-Property → SA818-U, 400–480 MHz
    artifacts: [firmware, native_sim]
  # später: hf-… (nur ein Eintrag)
```

- **Start-Targets:** `fm-sa818-2m` + `fm-sa818-70cm` (beide heute baubar).
- **Varianten-Mechanismus (geklärt):** die DT-`band`-Property am `sa818`-Node ist eine **Build-Zeit-Konstante** (`DT_ENUM_IDX`) und setzt Modell-String, Default-Frequenz und Frequenzbereiche. Das Manifest wählt `band=vhf|uhf` (via Board-Revision/Overlay); der Build appliziert sie.

## 5. Describe-fähige Targets

`CONFIG_MODULE_SA818=y` (+ zugehörige `CONFIG_MODULE`/CBPRINTF-FP-Settings) wandert nach **`app/prj.conf`** — so kennt das geshippte `app`-Binary die Modul-Plattform, nicht nur der Twister-Test. Beide Artefakt-Klassen (real + native_sim) erben das.

**Describe-Contract (Source of Truth: `tests/sim_shell`):** das `module describe`-Kommando am `fm> `-Prompt → Zeile `MODULE-DESCRIBE <json>` mit `identity.type=fm_transceiver`; `identity.version` = band (vhf/uhf).

## 6. native_sim: Static-Build

Ziel: **statisch gelinktes** native_sim-ELF, keine externen Lib-Abhängigkeiten → läuft unter jedem Ziel-glibc (Yocto scarthgap) ohne Container-Gymnastik.

- **Weg (a):** `-static` mit Host-glibc — einfach; für native_sim meist tragfähig (kein NSS/Name-Resolution im Sim).
- **Weg (b) Fallback:** vollstatisch gegen **musl** (garantiert sauber, mehr Toolchain-Setup).
- **Offene Verifikation (Implementierung):** Static-Linking real bauen + starten; (a)→(b)→(glibc-Container als letzter Fallback). Die funktionierende Link-Konfiguration dokumentieren.

## 7. Assets & Naming

Pro Target, benannt aus dem `name`-Feld:

| Klasse | Asset | Zweck |
|---|---|---|
| Real | `fm-sa818-2m.bin` | DFU/Flash (U575) |
| Real | `fm-sa818-2m.elf` | Debug/Symbols |
| Sim | `fm-sa818-2m.native_sim` | statisches x86-64-ELF für die D2-Sim-Schicht |

Plus ein **`SHA256SUMS`**-Manifest über alle Assets. (70cm analog.) Jedes Binary trägt die gestampte Release-Version (§3).

## 8. Signierung & Integrität

- **cosign keyless** signiert die Release-Assets (wie linux-image).
- **sha256 je Asset** (im Manifest) — Basis fürs Pinning.

## 9. Konsum / Bundle (linux-image)

- **linux-image bündelt BEIDE Klassen** — **reales Firmware *und* native_sim**, pro Board/Variante — per **URL + sha256-Pin** (Recipes `oe5xrx-fm-firmware` + `oe5xrx-native-sim-fm`, Scripts `scripts/pin-fm-firmware.sh` / `pin-native-sim-fm.sh`), exakt dem station-agent-SRCREV-Pin-Muster nachempfunden (Lockfile-Disziplin, kein floating).
- **Ergebnis: ein Bundle** — Image + Firmware werden **zusammen deployed und getestet** (eine getestete Einheit). Das reale Firmware im Image ist die Quelle für Bench-/On-Device-DFU.
- CI-Preflight (linux-image) lehnt ungepinnte/unsignierte Assets auf Release-Tags ab (analog zur SRCREV-AUTOREV-Sperre).
- FW-OTA übers station-manager = **später**.

## 10. Scope: jetzt vs. später

| | Jetzt | Später |
|---|---|---|
| Targets | fm-sa818-2m + fm-sa818-70cm | HF (Manifest-Eintrag), weitere Boards |
| Artefakte | real (`.bin`/`.elf`) + native_sim (static), version-gestampt | — |
| Bundle | linux-image pinnt real + native_sim (ein getestetes Bundle) | — |
| Verteilung | signiertes Release; Real = Bench-DFU aus dem Image | FW-OTA übers station-manager |
| Sicherheit | Asset-Signierung (cosign) | On-Device-Secure-Boot |

## 11. Risiken & Mitigationen

| Risiko | Mitigation |
|---|---|
| native_sim static nicht baubar (glibc) | Weg (a)→(b musl)→(Container-Fallback); früh verifizieren, dokumentieren. |
| Describe-Config driftet (app vs test) | `CONFIG_MODULE_SA818` in `app/prj.conf` → eine Wahrheit; Twister testet dasselbe app. |
| Version-Stamp inkonsistent (Tag ≠ Firmware) | Der eine Release-Job berechnet die Version **einmal** und speist Tag, Asset-Namen und Version-File aus derselben Quelle. |
| Matrix wächst unkontrolliert | Manifest ist explizit + reviewbar; ein Board = ein Eintrag. |
| glibc-Kompat des ELF im Yocto-Image | Durch Static-Build adressiert (Kern des Designs). |

## 12. Testing

- **Release-Workflow (dry-run/PR):** Matrix baut alle Targets; Assets entstehen + sind korrekt benannt; `SHA256SUMS` stimmt.
- **Version-Stamp:** die gebaute Firmware meldet zur Laufzeit die Release-Version (`YYYY.MM.DD-NN`) — matcht Tag + Asset-Namen.
- **Describe-Smoke:** das native_sim-Asset startet und antwortet auf `module describe` mit `MODULE-DESCRIBE …` (identity.type=fm_transceiver) — für 2m *und* 70cm die passende `version` (vhf/uhf).
- **Static-Check:** `ldd <asset>` meldet „not a dynamic executable".
- **Pin-Roundtrip:** `pin-*.sh` zieht URL+sha256, linux-image baut das Bundle dagegen.

## 13. Implementierung (Prozess)

superpowers-/CLAUDE.md-Fluss (Brainstorming = dieses Dokument, erledigt):
1. `superpowers:writing-plans` — Plan (release.yml Dispatch-Job, release-targets.yaml, Version-Stamping, app/prj.conf, Static-Build, cosign, Naming, linux-image Pin-Recipes/Scripts + Bundle) mit Task-Checkboxen.
2. `superpowers:subagent-driven-development` (forge für CI/Yocto/Build, rhythm für Zephyr-Config/Static/Version-Header).
3. `superpowers:test-driven-development` — Version-Stamp + Describe-Smoke + Static-Check + Pin-Roundtrip.
4. `superpowers:verification-before-completion`, dann PR(s) + copilot-loop.

Repos: `FW-RemoteStation` (Release-Job, Manifest, app-Config, Static-Build, Version-Stamping); `linux-image` (Pin-Recipes/Scripts, Bundle, Preflight). Ein Deliverable-Branch je Repo, ein PR je Repo.

---

*Diese Spec beschreibt den generischen FW-Release-Prozess (alle Boards, Sim + Real, board-benannte + version-gestampte Assets, static native_sim, signiert, pinbar) und das linux-image-Bundle (Image + Firmware zusammen). Erste Umsetzung deckt beide FM-Varianten ab; weitere Boards sind ein Manifest-Eintrag. Folgt dem Muster spec → plan → code.*
