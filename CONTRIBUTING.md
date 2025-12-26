# Contributing to FW-RemoteStation

Vielen Dank für dein Interesse an der Mitarbeit an **FW-RemoteStation**.

Dieses Projekt legt großen Wert auf Offenheit, Testbarkeit und langfristige Wartbarkeit.
Bitte lies dieses Dokument sorgfältig, bevor du Änderungen einreichst.

---

## Lizenz

Dieses Projekt steht unter der **GNU Lesser General Public License v3.0 (LGPL-3.0-or-later)**.

Mit dem Einreichen eines Beitrags erklärst du dich damit einverstanden, dass:

- dein Beitrag unter LGPL-3.0-or-later veröffentlicht wird
- du das Recht hast, den Code einzureichen
- keine inkompatibel lizenzierten Bestandteile eingebracht werden

Beiträge ohne klare Lizenzkompatibilität können nicht akzeptiert werden.

---

## Projektphilosophie

- Open Source steht im Vordergrund
- kommerzielle Nutzung ist erlaubt
- proprietäre Weiterentwicklungen des Codes sind nicht gewünscht
- Änderungen und Verbesserungen sollen der Community zugutekommen
- Simulation und Tests sind gleichwertig zur Hardware

---

## Technische Grundsätze

### Programmiersprache

- Primär: C++ (im Zephyr-Kontext)
- C ist erlaubt, wenn technisch sinnvoll

### Speicherverwaltung

- Keine dynamische Speicherallokation
- Verboten sind u. a.:
  - new / delete
  - malloc / free
  - std::vector
  - std::string
- Erlaubt sind u. a.:
  - std::array
  - constexpr
  - statische Speicherstrukturen
  - Value-Types

### Exceptions und RTTI

- Exceptions werden nicht verwendet
- RTTI ist nicht vorgesehen
- Code muss ohne diese Features korrekt funktionieren

---

## Simulation vs. Hardware

- Neue Funktionalität soll nach Möglichkeit zuerst in **native_sim** umgesetzt werden
- native_sim dient als:
  - Entwicklungsplattform
  - Systemtest-Umgebung
  - CI-Testziel
- Hardware-spezifischer Code muss klar gekapselt sein

---

## Tests (Pflicht)

Beiträge dürfen bestehende Tests nicht verschlechtern.

Erwartet wird:

- erfolgreiche Builds für:
  - fm_board
  - native_sim/native/64
- keine Regressionen in bestehenden Tests

Empfohlene lokale Tests:

```
west build -b fm_board app
west build -b native_sim/native/64 app

west twister -T app --integration -v
west twister -T tests/sim_shell -p native_sim/native/64 -v
west twister -T tests/unit_audio -p native_sim/native/64 -v
```

---

## Code-Stil

- konsistenter, gut lesbarer Code
- keine Magic Numbers (constexpr verwenden)
- sprechende Bezeichner
- einfache, nachvollziehbare Abstraktionen
- clang-format wird verwendet und in CI geprüft

Beiträge sollten vor dem Commit lokal formatiert werden.

---

## Commits und Pull Requests

- kleine, logisch getrennte Commits
- aussagekräftige Commit-Messages
- Pull Requests sollten:
  - beschreiben, was geändert wurde
  - erklären, warum die Änderung sinnvoll ist
  - relevante Tests nennen oder ergänzen

---

## Nicht-Ziele

In der Regel nicht akzeptiert werden:

- proprietäre oder geschlossene Abhängigkeiten
- unnötige Komplexität
- Änderungen ohne Teststrategie
- Code, der nur auf echter Hardware testbar ist

---

## Fragen und Diskussionen

Bei Unklarheiten oder größeren Änderungen bitte:

- ein Issue eröffnen
- oder vorab diskutieren, bevor umfangreiche Umbauten erfolgen

---

Vielen Dank für deinen Beitrag  
und dafür, **FW-RemoteStation** offen, testbar und wartbar zu halten.
