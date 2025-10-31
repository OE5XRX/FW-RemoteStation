# FW-RemoteStation

Firmware for the **OE5XRX Remote Station System**, providing embedded software for modular hardware components such as:

- FM Transceiver Module (SA818-based)


## 📦 Project Structure

```
firmware/
├── cm4/             # Firmware for CM4 carrier board
├── fm/              # Firmware for FM transceiver module
├── bus/             # Firmware for the bus board
├── tester/          # Firmware for the device tester
├── common/          # Shared code (e.g. logging, CLI, drivers)
└── docs/            # Optional documentation
```


## ⚙️ Build System

This project uses [CMake](https://cmake.org/) and [Dev Container](https://code.visualstudio.com/docs/devcontainers/containers) to build the firmware.


## 🚀 Features

- Modular FreeRTOS architecture
- Static memory allocation and more or less dynamic heap usage
- Unified CLI over USB via [TinyUSB](http://www.tinyusb.org)
- Built-in diagnostics and command shell
- Optional runtime stats and task monitoring


## 🧪 Development & Testing

- Target platform: STM32F302CBT6
- Communication: USB CDC
- Debugging via SWD and serial console
- Unit testing via [Cpputest](https://cpputest.github.io/)
- static code analysis via [Cppcheck](https://cppcheck.sourceforge.io/)


## 🛠️ Getting Started

### Dependencies

- Docker
- Editor with devcontainer support (VSCode)


### Start Devcontainer

Open the Devcontainer in our editor or start it via docker:

```bash
docker run -it -rm ghcr.io/oe5xrx/<IMAGE>:latest bash
```

### Build

There are multiple options to build the firmware and unittests.

#### Build Firmware

```bash
cmake --workflow --fresh --preset cross-compile-<release/build>
```


#### Build and Run Unittests

```bash
cmake --workflow --fresh --preset unittest-<release/build>
```
