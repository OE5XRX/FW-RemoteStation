# FW-RemoteStation

Zephyr-RTOS-Firmware für den STM32-Teil der OE5XRX Remote-Station: FM-Transceiver-Modul auf Basis **SA818**, mit **STM32U575** als USB-Bridge (UAC2 + CDC-ACM + DFU). Meta-Projektbasis: `../CLAUDE.md`.

## Sprache & Coding-Standard

**Primär C++ im Zephyr-Kontext** (siehe auch `CONTRIBUTING.md`). Regel für Agenten:

- **Modernes C++ (C++17/20) für App- und Modul-Abstraktions-Logik.** OOP, RAII, Typsicherheit, `constexpr`, Namespaces, `std::` (vector/string/array/span/optional …), Vererbung **wo sie ein echtes Abstraktions-Interface trägt** (z.B. der generische `module`-Layer der Modul-Plattform). Kein plain-C-Stil in neuem Logik-Code.
- **Zephyr-C-Idiom an der HW-/Driver-Grenze respektieren.** Device-Model (`struct device`, API-Structs), Devicetree und Zephyr-APIs sind C-first. Dort **keine** vtables/Vererbung in die Driver-ABI zwingen — modernes C++ *innerhalb* der Implementierung ist ok, die Zephyr-Schnittstelle bleibt idiomatisch. „Modernes C++ wo es die Logik trägt, Zephyr-C wo das Framework es verlangt."
- **Bestehende Treiber NICHT refactoren.** Der aktuelle SA818-Treiber (`drivers/radio/sa818/`) ist bewusst im C-Style gehalten und funktioniert — nur wo nötig erweitern, nicht anfassen. Modernisierung nur opportunistisch, nie als Stil-Sweep unter Deadline. **Neuer Code = modern; alter, laufender Code = in Ruhe.**

## Struktur (Orientierung)

- `drivers/radio/sa818/` — SA818-Treiber (core/at/audio/shell), Header unter `drivers/radio/sa818/sa818/`.
- `app/src/` — Applikation (main, USB-Audio-Bridge, `sim_audio`).
- `boards/oe5xrx/fm_board/` — Custom-Board. Port F302→U575 läuft (PR #37); Active-Slot-/HW-Details dort.
- `tests/sim_shell/`, `tests/usb_audio/` — pytest + `native_sim` Test-Harness. **Neuer Code wird hier getestet.**
- `dts/bindings/radio/sa,sa818.yaml` — Devicetree-Binding.

## Modul-Plattform

Die Firmware ist die **dünne, self-describing Geräteschicht** der Modul-Plattform — **kein** Plattform-Wissen, keine Capability-Persistenz, kein Access-Model in der FW. Cross-Repo-Contract: `../station-manager/docs/superpowers/specs/2026-06-21-module-platform-sim-bridge-design.md` (§5.2 Firmware dünn, §8 Schnittstellen-Verträge).

## CI

`build_and_tests` **und** `clang_format` müssen grün sein — clang-format ist Pflicht.
