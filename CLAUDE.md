# FW-RemoteStation

Zephyr-RTOS-Firmware für den STM32-Teil der OE5XRX Remote-Station: FM-Transceiver-Modul auf Basis **SA818**, mit **STM32U575** als USB-Bridge (UAC2 + CDC-ACM + DFU).

## Sprache & Coding-Standard

**Primär C++ im Zephyr-Kontext.** Verbindliche Details in `CONTRIBUTING.md` — hier das Wichtigste für Agenten:

- **Modernes C++ (C++17/20) für App- und Modul-Abstraktions-Logik.** OOP, RAII, Typsicherheit, `constexpr`, Namespaces, Value-Types, Vererbung **wo sie ein echtes Abstraktions-Interface trägt**. Kein plain-C-Stil in neuem Logik-Code.
- **Keine dynamische Speicherallokation** (harte Repo-Regel). Verboten: `new`/`delete`, `malloc`/`free`, `std::vector`, `std::string`. Ebenso Typen meiden, die intern Heap allozieren können (`std::list`, `std::map`, `std::function`, …). Stattdessen statisch/stack: `std::array`, `std::span`, `std::string_view`, `std::optional`, `constexpr`, feste Puffer, Value-Types.
- **Keine Exceptions, kein RTTI** (Repo-Regel). Fehlerbehandlung über Rückgabe-/Status-Typen; kein `dynamic_cast`. Code muss ohne diese Features korrekt sein.
- **Zephyr-C-Idiom an der HW-/Driver-Grenze respektieren.** Device-Model (`struct device`, API-Structs), Devicetree und Zephyr-APIs sind C-first — dort keine vtables/Vererbung in die Driver-ABI zwingen; modernes C++ *innerhalb* der Implementierung ist ok, die Zephyr-Schnittstelle bleibt idiomatisch. Leitsatz: modernes C++ wo es die Logik trägt, Zephyr-C wo das Framework es verlangt.

## Struktur (Orientierung)

- `drivers/radio/sa818/` — SA818-Treiber (core/at/audio/shell), Header unter `drivers/radio/sa818/sa818/`.
- `app/src/` — Applikation (main, USB-Audio-Bridge, `sim_audio`).
- `boards/oe5xrx/fm_board/` — Custom-Board (STM32U575).
- `tests/sim_shell/`, `tests/usb_audio/` — pytest + `native_sim` Test-Harness. **Neuer Code wird hier getestet.**
- `dts/bindings/radio/sa,sa818.yaml` — Devicetree-Binding.

## Firmware-Rolle: dünn & self-describing

Die Firmware bleibt bewusst **dünn**: sie **beschreibt sich selbst** (meldet Identity + Fähigkeiten maschinenlesbar) und **führt Kommandos aus** — mehr nicht. **Kein** Plattform-Wissen, **keine** Capability-Persistenz, **kein** Access-/Rechte-Model in der Firmware. Solche Logik lebt außerhalb (Agent/Server). Neuer FW-Code soll diese Grenze wahren und nichts davon in die Firmware ziehen.

## CI

`build_and_tests` **und** `clang_format` müssen grün sein — clang-format ist Pflicht.
