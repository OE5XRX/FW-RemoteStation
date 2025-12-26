# FW-RemoteStation

Firmware für die **OE5XRX Remote Station** auf Basis von **Zephyr RTOS**.

Dieses Projekt implementiert eine modulare, testbare Firmware für ein FM-Transceiver-Board (`fm_board`).
Zusätzlich wird **native_sim** verwendet, um Logik, Audio-Pfade und Protokolle frühzeitig und automatisiert
zu testen – noch bevor echte Hardware erforderlich ist.

---

## Projektziele

- Klare Trennung von Hardware-abhängigem Code und Logik
- High-Level-Tests mittels Simulation (`native_sim`)
- Reproduzierbare Builds und Tests mit **Twister**
- Verwendung von **C++ ohne Heap**
- Fokus auf Offenheit, Wartbarkeit und Langzeitbetrieb

---

## Unterstützte Targets

- fm_board  
  STM32F302-basiertes FM-Transceiver-Board

- native_sim/native/64  
  Host-Simulation (Linux), primär für Tests und Entwicklung

---

## Projektstruktur

```
.
├── app/
│   ├── src/                Firmware-Quellcode (C++)
│   ├── boards/             Board-spezifische Overlays
│   └── sample.yaml         Twister Integration
│
├── boards/oe5xrx/
│   └── fm_board/           Custom Zephyr Board
│
├── tests/
│   ├── sim_shell/          Systemtests (pytest + Twister)
│   └── unit_audio/         Unit-Tests (ztest)
│
├── .devcontainer/          Devcontainer (lokale Entwicklung)
├── .github/workflows/      CI (GitHub Actions)
├── west.yml
└── README.md
```

---

## Abhängigkeiten

- Zephyr RTOS (via west)
- Python >= 3.10 (Twister, pytest)
- GNU Arm Embedded Toolchain oder Zephyr SDK
- Linux Host für native_sim

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

### Hardware (fm_board)

```
west build -b fm_board app
```

### Simulation (native_sim)

```
west build -b native_sim/native/64 app
./build/zephyr/zephyr.exe
```

---

## Simulation Features (native_sim)

- UART Shell über stdin/stdout
- ADC-Emulation
- EEPROM-Simulation
- WAV-Playback
- Sinusgenerator für Audio-Tests

Beispiel:

```
fm> wav sine 1000 1.0 8000
fm> adc_read
fm> wav stop
```

---

## Tests

### Integration & Systemtests

```
west twister -T app --integration -v
west twister -T tests/sim_shell -p native_sim/native/64 -v
```

### Unit-Tests

```
west twister -T tests/unit_audio -p native_sim/native/64 -v
```

---

## Design-Prinzipien

- C++ ohne Heap (keine dynamische Speicherallokation)
- Simulation ist gleichwertig zur Hardware
- Testbarkeit vor Optimierung
- Explizite Abhängigkeiten und klare Abstraktionen

---

## Lizenz

Dieses Projekt ist unter der **GNU Lesser General Public License v3.0 (LGPL-3.0-or-later)** lizenziert.

Der Open-Source-Gedanke steht im Vordergrund:
Änderungen und Verbesserungen am Code sollen der Community wieder zur Verfügung stehen.

---

## Status

Aktiv in Entwicklung  
Fokus: Simulation, Tests, CI-Stabilität

---

## Kontakt

Amateurfunkclub für Remote Stationen  
Rufzeichen: **OE5XRX**
