# FM Board USB Configuration

Der FM Board verwendet ein USB Composite Device mit drei Funktionen:

## USB Interfaces

### 1. CDC ACM (Communications Device Class - Abstract Control Model)
- **Zweck**: Shell/Console Interface für Kommandos und Debug-Output
- **Device Class**: 02h (Communications)
- **Verwendung**: 
  - Über `/dev/ttyACMx` (Linux) oder `COMx` (Windows) zugreifbar
  - Zephyr Shell für AT-Kommandos und Diagnose
  - Logging-Output

### 2. UAC2 (USB Audio Class 2)
- **Zweck**: Audio-Streaming zwischen Host und SA818
- **Device Class**: 01h (Audio)
- **Konfiguration**:
  - Sample Rate: 8 kHz (SA818-kompatibel)
  - Format: 16-bit PCM, Mono
  - Bidirektional:
    - **OUT** (Playback): USB Host → SA818 TX (Audio zum Senden)
    - **IN** (Capture): SA818 RX → USB Host (empfangenes Audio)
- **Verwendung**:
  - Standard-Audio-Device unter Linux (ALSA), Windows (DirectSound), macOS (CoreAudio)
  - Keine Treiber erforderlich (driverless)
  
  Beispiel Linux:
  ```bash
  # Audio zur SA818 senden (Transmission)
  aplay -D hw:CARD=OE5XRX,DEV=0 -f S16_LE -r 8000 -c 1 audio.wav
  
  # Audio von SA818 aufnehmen (Reception)
  arecord -D hw:CARD=OE5XRX,DEV=0 -f S16_LE -r 8000 -c 1 received.wav
  ```

### 3. DFU (Device Firmware Update)
- **Zweck**: Firmware-Updates über USB
- **Device Class**: FEh (Application Specific - DFU)
- **Verwendung**:
  ```bash
  # Mit dfu-util (Linux/macOS/Windows)
  dfu-util -a 0 -D firmware.bin
  ```
- **Boot-Modi**:
  - Normal: Alle drei Interfaces aktiv (CDC ACM + UAC2 + DFU)
  - DFU-Modus: Nur DFU-Interface aktiv (nach Reset oder DFU-Detach)

## USB Descriptor

```
Device Descriptor:
  bcdUSB             2.00
  bDeviceClass       239 (Miscellaneous)
  bDeviceSubClass      2
  bDeviceProtocol      1
  idVendor         0x2fe3 (Zephyr Project)
  idProduct        0x0100 (OE5XRX FM Remote Station)
  iManufacturer       1 (OE5XRX)
  iProduct            2 (OE5XRX FM Remote Station)
  
  Configuration Descriptor:
    Interface 0: CDC ACM Control
    Interface 1: CDC ACM Data
    Interface 2: Audio Control
    Interface 3: Audio Streaming OUT (USB -> SA818)
    Interface 4: Audio Streaming IN (SA818 -> USB)
    Interface 5: DFU Runtime
```

## Device Tree Konfiguration

Die USB-Konfiguration erfolgt über:
- **Base DTS** (`fm_board.dts`): USB-Peripheral, CDC ACM und UAC2 Audio-Streaming

### UAC2 Audio-Terminals

```
USB OUT → usb_out_terminal → sa818_tx_output → SA818 DAC (TX)
SA818 ADC (RX) → sa818_rx_input → usb_in_terminal → USB IN
```

## Kconfig-Optionen

Siehe `fm_board_defconfig`:
```kconfig
CONFIG_USBD_CDC_ACM_CLASS=y      # Shell/Console
CONFIG_USBD_AUDIO2_CLASS=y       # Audio Streaming
CONFIG_USBD_DFU=y                # Firmware Update
```

## Integration mit SA818

### Audio-Datenfluss

**Transmission (PTT ON)**:
1. Host sendet Audio via USB UAC2 OUT
2. Zephyr USB Audio Stack → Audio-Buffer
3. SA818 Audio Subsystem → DAC
4. DAC → SA818 TX Modulator

**Reception (PTT OFF)**:
1. SA818 RX Demodulator → ADC
2. ADC → SA818 Audio Subsystem
3. Audio-Buffer → Zephyr USB Audio Stack
4. USB UAC2 IN → Host

### Implementierungs-Hinweise

Die Integration von UAC2 mit SA818 erfordert:
1. **Audio-Callback-Implementation** in `sa818_audio.cpp`:
   - `uac2_data_request_cb()` - UAC2 fordert TX-Audio an
   - `uac2_data_written_cb()` - UAC2 hat RX-Audio geliefert

2. **Buffer-Management**:
   - Ring-Buffer für Audio-Streaming
   - Synchronisation zwischen USB und DAC/ADC

3. **Sample-Rate Conversion** (falls benötigt):
   - USB: 8000 Hz (UAC2 konfiguriert)
   - SA818: 8000 Hz (nativ)
   - → Keine Konversion notwendig ✓

## Test und Verifizierung

### Enumeration testen
```bash
# Linux
lsusb -d 2fe3:0100 -v

# Alle drei Interfaces sollten sichtbar sein:
# - ttyACM0 (CDC ACM)
# - card X (UAC2)
# - dfu device (DFU)
```

### Audio-Loopback-Test
```bash
# Terminal 1: Empfangen
arecord -D hw:CARD=OE5XRX -f S16_LE -r 8000 -c 1 | aplay

# Terminal 2: Shell-Kommandos
screen /dev/ttyACM0 115200
sa818 ptt on
```

## Lizenz

Copyright (c) 2025 OE5XRX  
SPDX-License-Identifier: LGPL-3.0-or-later
