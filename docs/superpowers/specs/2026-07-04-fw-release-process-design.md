# Design: FW-RemoteStation Release-Prozess

**Datum:** 2026-07-04
**Status:** Design (genehmigt, vor Implementierung)
**Repo:** `FW-RemoteStation` (Release-Workflow, Target-Manifest, App-Config, Static-Build); Konsum-Seite in `linux-image` (Pin-Recipe + Script)
**Bezug:** entblockt D2 (`2026-07-04-module-simulation-layer-design.md`) — die Sim-Schicht braucht ein gepinntes `native_sim`-FM-Artefakt. Richtet sich an der Projekt-Release-Kultur aus (linux-image datumsbasierte Releases, station-agent SRCREV-Pin, HW-Module-CI signierte Releases, cosign).

---

## 1. Ziel & Kontext

**Problem:** FW-RemoteStation hat **keinen Release-Prozess**. `ci.yml` macht nur clang-format + native_sim-Build + Twister-Tests — **kein Artifact-Upload, kein reales `fm_board`-Build, keine Signierung, keine Tags/Releases**. Damit gibt es kein definiertes Artefakt für (a) die D2-Sim-Schicht (`native_sim`-Binary) und (b) DFU/Flash am Bench (reales U575-Binary).

**Zusatz-Kontext:** FW-RemoteStation ist ein **Monorepo für alle Board-Firmwares** (FM, künftig HF …). Varianten (2m/70cm) werden über den **Devicetree** unterschieden (gleiche PCB, bestückt für SA818-V/-U + passendem Filter). Ein Release muss daher Artefakte **pro Board/Variante** liefern, klar benannt.

## 2. Getroffene Entscheidungen

| Frage | Entscheidung | Begründung |
|---|---|---|
| Scope | **Sim + Real** in einem Release | Beide Konsumenten (D2-Sim, Bench-DFU) an einem Tag versorgt; Real nicht nachträglich reingezwängt. |
| Board-Abdeckung | **Monorepo, Artefakte pro Board/Variante** benannt | Alle Firmwares in einem Repo, DT-Varianten; „fm sa818 2m" muss sofort auffindbar sein. |
| Target-Definition | **Deklaratives `release-targets.yaml`** (Ansatz A) | Single-Source-of-Truth „was wird released"; neues Board = ein Eintrag; CI generisch. |
| Versionierung | **`YYYY.MM.DD-NN`**, repo-weit (eine Version für alle Boards) | Identisch zu linux-image (z.B. `2026.04.24-18`); ein Release-Event deckt alle Targets. |
| Describe-Fähigkeit | **`CONFIG_MODULE_SA818` in `app/prj.conf`** | „geshipptes = getestetes" Binary; beide Klassen beantworten `module fm describe`. |
| native_sim-Linking | **Statisch** (keine externen Lib-Deps) | Löst die glibc-Kopplung; ELF läuft in jedem Image. |
| Signierung | **cosign keyless** auf die Assets + sha256 | Wie linux-image; Assets verifizierbar + pinbar. |
| Konsum | **Pin per URL+sha256** (Recipe + Script) | Analog station-agent-SRCREV-Pin; Lockfile-Disziplin. |

## 3. Versionierung & Trigger

- Schema **`YYYY.MM.DD-NN`** (Datum + Tages-Sequenz), **eine repo-weite Version** — nicht per-Board.
- Ein **Git-Tag** triggert einen eigenen **Release-Workflow** (`.github/workflows/release.yml`, neben dem bestehenden `ci.yml`). Alle Targets werden am Tag gebaut, signiert, als ein GitHub-Release veröffentlicht.
- linux-image hat eine `release.yml` als Vorlage — Muster übernehmen.

## 4. Build-Matrix — `release-targets.yaml` (Ansatz A)

Eine Datei im Repo listet die Release-Ziele; der Workflow iteriert darüber:

```yaml
targets:
  - name: fm-sa818-2m
    board: fm_board          # + Varianten-Selektion (DT-Overlay/Kconfig) für SA818-V / VHF
    variant: vhf
    artifacts: [firmware, native_sim]
  - name: fm-sa818-70cm
    board: fm_board
    variant: uhf
    artifacts: [firmware, native_sim]
  # später: hf-… (nur ein Eintrag)
```

- **Start-Targets:** `fm-sa818-2m` + `fm-sa818-70cm` (beide heute baubar).
- Der genaue Varianten-Mechanismus (Zephyr-Board-Variante vs. DT-Overlay vs. Kconfig-Choice) wird in der Implementierung festgelegt; das Manifest kapselt ihn.

## 5. Describe-fähige Targets

`CONFIG_MODULE_SA818=y` (+ `CONFIG_MODULE=y`, CBPRINTF-FP für die `%.4f`-Floats im `MODULE-RESULT`) wandert nach **`app/prj.conf`** — so kennt das geshippte `app`-Binary `module fm describe`, nicht nur der Twister-Test. Beide Artefakt-Klassen (real + native_sim) erben das.

**Describe-Contract (Source of Truth: `tests/sim_shell`):** Kommando `module fm describe` am `fm> `-Prompt → Zeile `MODULE-DESCRIBE <json>` mit `identity.type=fm_transceiver`.

## 6. native_sim: Static-Build

Ziel: **statisch gelinktes** native_sim-ELF, keine externen Lib-Abhängigkeiten → läuft unter jedem Ziel-glibc (Yocto scarthgap) ohne Container-Gymnastik.

- **Weg (a):** `-static` mit Host-glibc — einfach; für native_sim meist tragfähig (kein NSS/Name-Resolution im Sim).
- **Weg (b) Fallback:** vollstatisch gegen **musl** (garantiert sauber, mehr Toolchain-Setup).
- **Offene Verifikation (Implementierung):** Static-Linking von native_sim real bauen + starten; falls (a) bricht → (b). Falls beides infeasible → glibc-Matching-Container als letzter Fallback. Die funktionierende Link-Konfiguration dokumentieren.

## 7. Assets & Naming

Pro Target, benannt aus dem `name`-Feld:

| Klasse | Asset | Zweck |
|---|---|---|
| Real | `fm-sa818-2m.bin` | DFU/Flash (U575) |
| Real | `fm-sa818-2m.elf` | Debug/Symbols |
| Sim | `fm-sa818-2m.native_sim` | statisches x86-64-ELF für die D2-Sim-Schicht |

Plus ein **Checksums-/Asset-Manifest** (`SHA256SUMS`) für alle Assets. (70cm analog.)

## 8. Signierung & Integrität

- **cosign keyless** signiert die Release-Assets (wie linux-image).
- **sha256 je Asset** (im Manifest) — Basis fürs Pinning.

## 9. Konsum / Pinning

- **linux-image** pinnt das benötigte `native_sim`-Asset per **URL + sha256** über eine Recipe (`oe5xrx-native-sim-fm`) + `scripts/pin-native-sim-fm.sh` — exakt dem station-agent-SRCREV-Pin-Muster nachempfunden (Dependency wie ein Lockfile, kein floating). CI-Preflight lehnt ungepinnte/unsignierte Assets ab.
- **Reales Firmware** = Download-Asset für manuelles Bench-DFU (dfu-util). FW-OTA übers station-manager ist **out of scope** (später).

## 10. Scope: jetzt vs. später

| | Jetzt | Später |
|---|---|---|
| Targets | fm-sa818-2m + fm-sa818-70cm | HF (Manifest-Eintrag), weitere Boards |
| Artefakte | real (`.bin`/`.elf`) + native_sim (static) | — |
| Verteilung | signiertes Release + linux-image-Pin-Script; Real = Bench-DFU | FW-OTA übers station-manager |
| Sicherheit | Asset-Signierung (cosign) | On-Device-Secure-Boot |

## 11. Risiken & Mitigationen

| Risiko | Mitigation |
|---|---|
| native_sim static nicht baubar (glibc) | Weg (a)→(b musl)→(Container-Fallback); früh verifizieren, Ergebnis dokumentieren. |
| Describe-Config driftet (app vs test) | `CONFIG_MODULE_SA818` in `app/prj.conf` → eine Wahrheit; Twister testet dasselbe app. |
| Varianten-Mechanismus unklar (VHF/UHF im DT) | Manifest kapselt ihn; genaue Zephyr-Board-Variante/Overlay in der Implementierung fixiert. |
| Matrix wächst unkontrolliert | Manifest ist explizit + reviewbar; ein Board = ein Eintrag, kein Pipeline-Umbau. |
| glibc-Kompat des ELF im Yocto-Image | Durch Static-Build adressiert (Kern des Designs). |

## 12. Testing

- **Release-Workflow (dry-run/PR):** Matrix baut alle Targets; Assets entstehen + sind korrekt benannt; `SHA256SUMS` stimmt.
- **Describe-Smoke:** das native_sim-Asset startet und antwortet auf `module fm describe` mit `MODULE-DESCRIBE …` (identity.type=fm_transceiver) — für 2m *und* 70cm die passende `version`.
- **Static-Check:** `ldd <asset>` meldet „not a dynamic executable" (bzw. keine externen Deps).
- **Pin-Roundtrip:** `pin-native-sim-fm.sh` zieht URL+sha256, linux-image baut dagegen.

## 13. Implementierung (Prozess)

superpowers-/CLAUDE.md-Fluss (Brainstorming = dieses Dokument, erledigt):
1. `superpowers:writing-plans` — Plan (release.yml, release-targets.yaml, app/prj.conf, Static-Build, cosign, Naming, linux-image Pin-Recipe/Script) mit Task-Checkboxen.
2. `superpowers:subagent-driven-development` (forge für CI/Yocto/Build, rhythm für Zephyr-Config/Static).
3. `superpowers:test-driven-development` — Describe-Smoke + Static-Check + Pin-Roundtrip.
4. `superpowers:verification-before-completion`, dann PR(s) + copilot-loop.

Repos: v.a. `FW-RemoteStation` (Workflow, Manifest, app-Config, Static-Build); Pin-Recipe/Script in `linux-image`. Ein Deliverable-Branch je Repo, ein PR je Repo.

---

*Diese Spec beschreibt den generischen FW-Release-Prozess (alle Boards, Sim + Real, board-benannte Assets, static native_sim, signiert, pinbar). Erste Umsetzung deckt beide FM-Varianten ab; weitere Boards sind ein Manifest-Eintrag. Folgt dem Muster spec → plan → code.*
