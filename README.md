[![CI](https://github.com/OE5XRX/FW-RemoteStation/actions/workflows/ci.yml/badge.svg)](https://github.com/OE5XRX/FW-RemoteStation/actions/workflows/ci.yml)
[![License: LGPL v3](https://img.shields.io/badge/license-LGPLv3-blue.svg)](LICENSE)
[![Zephyr RTOS](https://img.shields.io/badge/RTOS-Zephyr-blue)](https://zephyrproject.org/)

# FW-RemoteStation

---

## Was es ist

**FW-RemoteStation** ist die Zephyr-RTOS-Firmware für das FM-Transceiver-Board der OE5XRX Remote Station.
Sie betreibt einen **STM32U575** als USB-Bridge mit SA818-FM-Transceiver-Anbindung (Audio via UAC2,
Steuerung via CDC-ACM, Firmware-Update via DFU).

Das Target `native_sim/native/64` ermöglicht automatisierte Tests von Logik, Audio-Pfaden und
Protokollen auf dem Entwicklungsrechner — noch bevor echte Hardware erforderlich ist.

---

## Ziele

- Klare Trennung von Hardware-abhängigem Treiber-Code und anwendungsnaher Logik
- High-Level-Tests via Simulation (`native_sim`) ohne physische Hardware
- Reproduzierbare Builds und Tests mit Twister
- Zweistufige Sprachpolitik: Treiber in `drivers/` folgen dem C-first-Zephyr-Idiom (C-ABI —
  die Implementierung darf `.cpp` sein, erzwungen wird kein Sprachwechsel); modernes C++20 ohne
  Heap-Allokation für Nicht-Treiber-Code (App, Modul-Layer, USB, Simulation)
- Fokus auf Offenheit, Wartbarkeit und Langzeitbetrieb

---

## Unterstützte Targets

| Target                  | Beschreibung                                                             |
|-------------------------|--------------------------------------------------------------------------|
| `fm_board`              | STM32U575-basiertes FM-Transceiver-Board (echte Hardware)                |
| `native_sim/native/64`  | Host-Simulation auf Linux x86-64 (primär für Entwicklung, Tests und CI)  |

---

## Projektstruktur

```
.
├── app/
│   ├── src/                   Firmware-Quellcode (C++20)
│   │   └── sim_audio/         Simulation-Audio-Quellen (WAV, Sinus)
│   ├── boards/                Board-spezifische Overlays und Konfigurationen
│   └── sample.yaml            Twister-Integration
│
├── boards/oe5xrx/
│   └── fm_board/              Custom Zephyr Board (STM32U575)
│
├── drivers/
│   └── radio/
│       └── sa818/             SA818-Treiber (Core, AT, Audio, Audio-Stream, Shell) — C-ABI
│
├── subsys/
│   └── module/                Generischer Modul-Layer (Capability-Framework)
│       └── devices/           Geräte-spezifische Capability-Implementierungen
│
├── include/
│   └── oe5xrx/
│       └── module/            Öffentliche Header (iface.h, geräteunabhängig)
│
├── dts/
│   └── bindings/              Devicetree-Bindings für OE5XRX-spezifische Nodes
│
├── tests/
│   ├── sim_shell/             Systemtests (pytest + Twister, stdin/stdout-Shell)
│   ├── etl/                   ETL-Integrations- und Verhaltensnachweise
│   └── usb_audio/             USB-Audio-Tests
│
├── .github/
│   └── workflows/             CI-Workflows (GitHub Actions)
├── west.yml
└── README.md
```

---

## Abhängigkeiten

- **Zephyr RTOS** (via west, wird durch `west update` bezogen)
- **ETL** (Embedded Template Library) `20.48.0` (via west, Pfad `modules/lib/etl`)
- **Python >= 3.10** (für Twister und pytest)
- **Zephyr SDK** oder GNU Arm Embedded Toolchain
- **Linux Host** (erforderlich für `native_sim`)

---

## Setup

```
west init -m https://github.com/OE5XRX/FW-RemoteStation
cd FW-RemoteStation
west update
west zephyr-export
```

---

## Build

### Hardware (`fm_board`)

```
west build -b fm_board app
```

### Simulation (`native_sim`)

```
west build -b native_sim/native/64 app
./build/zephyr/zephyr.exe
```

---

## Simulation-Features (`native_sim`)

Die Simulation bildet die gesamte Firmware-Logik auf dem Linux-Host ab:

- UART-Shell über stdin/stdout
- ADC-Emulation
- EEPROM-Simulation
- WAV-Playback
- Sinusgenerator für Audio-Tests

Beispiel-Befehle in der Shell:

```
fm> wav sine 1000 1.0 8000
fm> adc_read
fm> wav stop
```

---

## Tests

### Integration und Systemtests mit Twister

```
west twister -T app --integration -v
west twister -T tests/sim_shell -p native_sim/native/64 -v
west twister -T tests/etl -p native_sim/native/64 -v
```

Die realen Test-Verzeichnisse sind `tests/etl`, `tests/sim_shell` und `tests/usb_audio`.

### pytest-Systemtests

Unter `tests/sim_shell/pytest/` treibt eine pytest-Testsuite die Firmware-Shell über
stdin/stdout an. Der enthaltene `SA818Simulator` implementiert das AT-Protokoll in Python und
ermöglicht vollständige Protokoll- und Capability-Tests ohne physische Hardware.

### CI-Gates

Jeder Pull Request und jeder Push auf `main` muss zwei CI-Jobs bestehen:

- **`clang_format`** — prüft alle C/C++-Dateien (`.c/.h/.cc/.hh/.cpp/.hpp`) unter `app/`,
  `boards/`, `tests/` mit clang-format-18; schlägt fehl, sobald eine Abweichung festgestellt wird.
- **`build_and_tests`** — baut `native_sim` und führt Twister auf `app --integration`,
  `tests/sim_shell` und `tests/etl` aus.

---

## Design-Prinzipien

### Zweistufige Sprachpolitik

Der Code folgt einer bewussten Zwei-Ebenen-Regel, die sich nach der **Schicht** richtet,
nicht nach der Dateiendung:

- **Treiber-Schicht (`drivers/`):** C-first Zephyr-Idiom — Zephyr-Gerätemodell (`struct device`,
  API-Structs), Devicetree-Makros, `extern "C"`-Header, Ergebnis-Enums. Kein erzwungenes C++
  in der Treiber-ABI.
- **Nicht-Treiber-Code (App, Modul-Layer, USB, Simulation):** Modernes C++20 mit ETL —
  OOP, RAII, `constexpr`, Namespaces, Werttypen; ETL-Container statt heap-gebundener
  `std::`-Äquivalente.

### Harte Regeln (gelten überall)

- **Keine dynamische Speicherallokation:** `new`, `delete`, `malloc`, `free`, `std::vector`,
  `std::string`, `std::list`, `std::map`, `std::function` und alle intern allokierenden Typen
  sind verboten.
- **Keine Ausnahmen (Exceptions):** Das Projekt wird mit deaktivierten Exceptions gebaut.
  Fehlerbehandlung erfolgt ausschließlich über Rückgabe-/Statustypen.
- **Kein RTTI:** Kein `dynamic_cast`, kein `typeid`.

### Simulation gleichwertig zur Hardware

Neue Funktionalität wird zuerst auf `native_sim` implementiert und getestet.
Hardware-spezifischer Code muss sauber gekapselt sein, damit die Logik-Schicht ohne
echte Hardware testbar bleibt.

### Testbarkeit vor Optimierung

Code, der ausschließlich auf echter Hardware testbar ist, ist ein Entwurfsproblem —
die Kapselung muss dann zuerst verbessert werden.

### Explizite Abhängigkeiten

Alle Abhängigkeiten werden über `west.yml` gepinnt (ETL `20.48.0`). Die Firmware bleibt
bewusst schlank: Sie beschreibt sich selbst (Identität und Capabilities als maschinenlesbares
JSON) und führt Befehle aus — keine Plattform-Logik, keine Persistenz, kein Zugriffsmodell
in der Firmware.

---

## Lizenz

Dieses Projekt steht unter der **GNU Lesser General Public License v3.0 oder neuer
(LGPL-3.0-or-later)**.

Änderungen und Verbesserungen am Code sollen der Community wieder zur Verfügung stehen.
Kommerzielle Nutzung ist erlaubt; proprietäre Forks des Codes sind unerwünscht.

---

## Status

Aktiv in Entwicklung. Schwerpunkte: Simulation, Tests, CI-Stabilität.

---

## Mitarbeit

Beiträge sind willkommen. Der verbindliche Coding-Standard, die Architektur-Übersicht,
Build- und Test-Workflow sowie die Commit- und PR-Regeln sind vollständig in
[`CONTRIBUTING.md`](CONTRIBUTING.md) beschrieben.

Für Fragen oder vor größeren Umbauten bitte zuerst ein Issue eröffnen.

---

Rufzeichen: **OE5XRX**
