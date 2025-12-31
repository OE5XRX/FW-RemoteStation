# USB Audio Bridge für SA818

USB Audio Class 2 (UAC2) Integration auf Application-Level.

## Architektur

**Saubere Trennung der Verantwortlichkeiten**:

```
┌─────────────────────────────────────────────────────────────────┐
│                        APPLICATION                               │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │            usb_audio_bridge.cpp                          │   │
│  │                                                          │   │
│  │  • Ring Buffers (USB ↔ SA818)                          │   │
│  │  • UAC2 Callbacks (USB-spezifisch)                     │   │
│  │  • SA818 Audio Callbacks (treiber-generisch)           │   │
│  │  • USB IN Thread                                        │   │
│  └──────────────────────────────────────────────────────────┘   │
│              ▲                              ▲                    │
│              │ USB                          │ SA818              │
│              │ (usbd_uac2_*)                │ (sa818_audio_*)   │
└──────────────┼──────────────────────────────┼──────────────────┘
               │                              │
┌──────────────┼──────────────────────────────┼──────────────────┐
│              │                              │                   │
│  ┌───────────▼──────────────┐   ┌──────────▼──────────────┐   │
│  │   Zephyr UAC2 Stack      │   │   SA818 Driver          │   │
│  │                          │   │                         │   │
│  │   • Terminal Management  │   │   • Generic Audio API   │   │
│  │   • USB Endpoints        │   │   • DAC/ADC Control     │   │
│  │   • Buffer Management    │   │   • Callback Interface  │   │
│  └──────────────────────────┘   └─────────────────────────┘   │
│                                                                 │
│                      HARDWARE / ZEPHYR                          │
└─────────────────────────────────────────────────────────────────┘
```

**Vorteile dieser Architektur**:
- ✅ SA818 Treiber ist USB-agnostisch (wiederverwendbar)
- ✅ Application hat volle Kontrolle über Audio-Routing
- ✅ Einfach zu erweitern (I2S, File Playback, Netzwerk, etc.)
- ✅ Bessere Testbarkeit durch Entkopplung
- ✅ Klare Verantwortlichkeiten

## Audio-Datenfluss

**Transmission (USB OUT → SA818 TX)**:
1. Host sendet Audio via USB Audio OUT (Playback)
2. UAC2 Stack empfängt Daten in `uac2_data_recv_cb()`
3. Daten werden in TX Ring Buffer geschrieben
4. `audio_work_handler()` liest aus Ring Buffer
5. 16-bit PCM wird zu DAC-Wert konvertiert
6. DAC schreibt zu SA818 TX Modulator

**Reception (SA818 RX → USB IN)**:
1. `audio_work_handler()` liest ADC-Wert
2. ADC-Wert wird zu 16-bit PCM konvertiert
3. PCM-Sample wird in RX Ring Buffer geschrieben
4. `usb_in_thread()` liest aus Ring Buffer
5. Daten werden via `usbd_uac2_send()` gesendet
6. Host empfängt Audio via USB Audio IN (Capture)

## Komponenten

### 1. SA818 Driver (Generic Audio Interface)

**Header**: `drivers/radio/sa818/include/sa818/sa818_audio_stream.h`

```cpp
/* Callback-Typen */
typedef size_t (*sa818_audio_tx_request_cb)(
    const struct device *dev, uint8_t *buffer, size_t size, void *user_data);

typedef void (*sa818_audio_rx_data_cb)(
    const struct device *dev, const uint8_t *buffer, size_t size, void *user_data);

/* API-Funktionen */
sa818_result sa818_audio_stream_register(
    const struct device *dev, const struct sa818_audio_callbacks *callbacks);

sa818_result sa818_audio_stream_start(
    const struct device *dev, const struct sa818_audio_format *format);

sa818_result sa818_audio_stream_stop(const struct device *dev);
```

**Eigenschaften**:
- Hardware-agnostisch (keine USB-Abhängigkeit)
- Pull-Model für TX (Driver fordert Daten an)
- Push-Model für RX (Driver liefert Daten)
- Konfigurierbare Sample Rate, Bit Depth, Channels

### 2. USB Audio Bridge (Application)
### 2. USB Audio Bridge (Application)

**Source**: `app/src/usb_audio_bridge.cpp`

**Verantwortlichkeiten**:
- Verbindet SA818 Audio Callbacks mit UAC2 Events
- Verwaltet Ring Buffer für USB ↔ SA818
- Handhabt UAC2 Terminal Activation
- USB IN Thread für Host-Streaming

**Ring Buffer**:
- **TX Ring**: 512 Bytes (256 Samples = 32ms @ 8kHz)
- **RX Ring**: 512 Bytes (256 Samples = 32ms @ 8kHz)
- Entkoppelt USB-Transfers von DAC/ADC-Zugriffen
- Toleriert Jitter und Timing-Unterschiede

### Audio Processing
- **Sample Rate**: 8000 Hz (passend zu SA818)
- **Format**: 16-bit signed PCM, Mono
- **Processing Rate**: 125µs pro Sample (8kHz)
- **Work Handler**: Delayable work, läuft mit 8kHz
- **USB Thread**: Separate Thread für USB IN mit 1ms Periode

### UAC2 Callbacks

```cpp
static const struct uac2_ops sa818_uac2_ops = {
    .sof_cb = uac2_sof_cb,                    // Start of Frame (1ms)
    .terminal_update_cb = uac2_terminal_update_cb,  // Terminal enable/disable
    .get_recv_buf = uac2_get_recv_buf,        // Buffer für USB OUT
    .data_recv_cb = uac2_data_recv_cb,        // USB OUT Daten empfangen
    .buf_release_cb = uac2_buf_release_cb,    // USB IN Buffer freigeben
};
```

## Verwendung

### 1. Device Tree Konfiguration

```dts
/* boards/oe5xrx/fm_board/fm_board_usb_composite.overlay */
uac2_radio: usb_audio2 {
    compatible = "zephyr,uac2";
    status = "okay";
    /* ... UAC2 configuration ... */
};
```

### 2. Application Code

```cpp
#include "usb_audio_bridge.h"
#include <sa818/sa818.h>

int main(void) {
    /* Get devices */
    const struct device *sa818 = DEVICE_DT_GET(DT_ALIAS(sa818));
    const struct device *uac2 = DEVICE_DT_GET(DT_NODELABEL(uac2_radio));
    
    /* Initialize USB */
    usbd_init(DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)));
    usbd_enable(DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)));
    
    /* Initialize USB Audio Bridge (connects SA818 ↔ UAC2) */
    int ret = usb_audio_bridge_init(sa818, uac2);
    if (ret != 0) {
        return ret;
    }
    
    /* Power on SA818 */
    sa818_set_power(sa818, SA818_DEVICE_ON);
    
    /* System ready - audio streaming active */
}
```

### 3. Build

```bash
# Mit USB Audio Overlay
west build -p -b fm_board/stm32f302xc app -- \
  -DEXTRA_DTC_OVERLAY_FILE="boards/oe5xrx/fm_board/fm_board_usb_composite.overlay"

# Flash
west flash
```

## Linux Host-Verwendung

### Audio abspielen (TX)

```bash
# WAV-Datei zur SA818 senden
aplay -D hw:CARD=OE5XRX,DEV=0 -f S16_LE -r 8000 -c 1 audio.wav

# Microphone live zur SA818 senden
arecord -D hw:1 -f S16_LE -r 48000 -c 1 | \
  sox -t raw -r 48000 -e signed -b 16 -c 1 - \
      -t raw -r 8000 -e signed -b 16 -c 1 - | \
  aplay -D hw:CARD=OE5XRX,DEV=0 -f S16_LE -r 8000 -c 1
```

### Audio aufnehmen (RX)

```bash
# Audio von SA818 empfangen und speichern
arecord -D hw:CARD=OE5XRX,DEV=0 -f S16_LE -r 8000 -c 1 -d 60 received.wav

# Audio von SA818 empfangen und live wiedergeben
arecord -D hw:CARD=OE5XRX,DEV=0 -f S16_LE -r 8000 -c 1 | \
  aplay -D hw:0 -f S16_LE -r 8000 -c 1
```

### PTT Steuerung

```bash
# Shell via CDC ACM
screen /dev/ttyACM0 115200

# In der Shell:
sa818 ptt on   # Transmission aktivieren
sa818 ptt off  # Reception aktivieren
```

### Loopback-Test

```bash
# Terminal 1: Audio empfangen und wiedergeben
arecord -D hw:CARD=OE5XRX,DEV=0 -f S16_LE -r 8000 -c 1 | \
  aplay -D hw:0 -f S16_LE -r 8000 -c 1

# Terminal 2: PTT ON und Audio senden
screen /dev/ttyACM0 115200
# In Shell: sa818 ptt on

# Terminal 3: Audio zur SA818 senden
aplay -D hw:CARD=OE5XRX,DEV=0 -f S16_LE -r 8000 -c 1 test_tone.wav
```

## Windows Host-Verwendung

### Audio Device auswählen
1. Windows erkennt "OE5XRX FM Remote Station" als Audio-Gerät
2. In Sound-Einstellungen als Standard-Gerät wählen
3. Audacity, SDR#, oder andere Software nutzen:
   - Sample Rate: 8000 Hz
   - Format: 16-bit PCM
   - Channels: Mono

### PTT Steuerung
```cmd
REM PuTTY oder TeraTerm für Shell
putty -serial COM3 -sercfg 115200,8,n,1,N
```

## Fehlerbehebung

### Kein Audio

1. **Terminal Status prüfen**:
   ```
   LOG_INF("USB OUT (TX) terminal enabled")
   LOG_INF("USB IN (RX) terminal enabled")
   ```

2. **Ring Buffer Status**:
   - Overflow → USB sendet zu schnell, Buffer zu klein
   - Underrun → DAC/ADC zu langsam, Processing-Rate erhöhen

3. **USB Enumeration**:
   ```bash
   lsusb -d 2fe3:0100 -v | grep -A5 "Audio"
   ```

### Audio-Qualität

- **Rauschen**: ADC-Referenz prüfen, Shielding verbessern
- **Verzerrung**: DAC-Skalierung anpassen, Amplitude reduzieren
- **Dropouts**: Ring Buffer Size erhöhen, Thread-Priorität anpassen

### Performance

- **CPU Load**: Work Handler Periode anpassen (aktuell 125µs)
- **Latenz**: Ring Buffer Size reduzieren (Trade-off mit Jitter-Toleranz)
- **Timing**: Thread-Prioritäten optimieren

## Architektur-Entscheidungen

### Warum Ring Buffer?
- Entkoppelt USB (asynchron) von DAC/ADC (periodisch)
- Toleriert USB Bus-Jitter und SOF-Timing-Variationen
- Vermeidet Buffer-Overflows bei Burst-Transfers

### Warum separate Threads?
- USB IN erfordert aktives Polling (keine Callback-basierte TX)
- Work Handler für DAC/ADC läuft mit fester Rate (8kHz)
- Separation verbessert Timing-Determinismus

### Warum 8kHz?
- SA818 Audio-Bandbreite: 300-3000 Hz
- Nyquist: 6kHz minimum → 8kHz ausreichend
- Passt zu Standard-Telefonie-Sample-Rate
- USB Full-Speed: 8 Samples/SOF @ 8kHz = perfekte Alignierung

## Lizenz

Copyright (c) 2025 OE5XRX  
SPDX-License-Identifier: LGPL-3.0-or-later
