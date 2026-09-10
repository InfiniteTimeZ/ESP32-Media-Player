# ESP32 Media Player

A desktop-connected media controller built around an ESP32-S3 touchscreen display.

The project combines a Windows desktop companion with an ESP32 touchscreen to display current media information, album artwork, system audio state, and optional Discord voice-channel information while also providing physical and touchscreen media controls.

<p align="center">
  <img src="hardware/images/display_showcase.jpg" alt="ESP32 Media Player" width="500">
</p>

---

## Documentation

| Guide | Purpose |
| --- | --- |
| [Quick Start](docs/QUICK_START.md) | Set up an assembled device |
| [Bill of Materials](hardware/BOM.md) | Parts and reference links |
| [Assembly Guide](hardware/ASSEMBLY.md) | Build the hardware |
| [3D Models](hardware/3d-models/) | Printable housing and stand |
| [Wiring Images](hardware/images/wiring/) | EC11 and CrowPanel pin references |

---

## Features

### Media Display

- Current track title and artist
- 250 × 250 album artwork
- Playback position and duration
- Dynamic interface colors derived from album artwork
- Current system time
- Windows volume and mute state
- Play/pause synchronization

### Controls

**EC11 rotary encoder**
- Rotate — adjust Windows system volume
- Single press — play/pause
- Double press — mute/unmute Windows audio

**Touchscreen**
- Play/pause
- Previous/next track
- Seeking
- Discord voice controls when Discord integration is enabled

### Discord Integration

Optional Discord integration provides:

- Current voice-channel name
- Voice-channel members
- User avatars
- Mute/deafen state
- Voice-channel controls

The interface displays up to **15 voice-channel users** at once.

---

## How It Works

The project is split into two applications:

1. A **Windows desktop companion**
2. Firmware running on the **ESP32-S3 display**

```text
                         Windows PC
┌────────────────────────────────────────────────────────────┐
│                    Desktop Companion                       │
│                                                            │
│  Windows Media ──► media.py                                │
│  Windows Audio ──► Pycaw                                   │
│  Discord ────────► discord_client.py / network.py          │
│                        │                                   │
│                        ▼                                   │
│                    hardware.py                             │
└───────────────────────────┬────────────────────────────────┘
                            │ USB Serial @ 230400 baud
                            ▼
┌────────────────────────────────────────────────────────────┐
│                         ESP32-S3                           │
│                serial_protocol.cpp                         │
│                         │                                  │
│              ┌──────────┴──────────┐                       │
│              ▼                     ▼                       │
│     media_display.cpp       discord_ui.cpp                 │
│                                                            │
│   Touchscreen / Encoder ──► commands back to Windows       │
└────────────────────────────────────────────────────────────┘
```

The desktop companion retrieves Windows and Discord state, sends it to the ESP32 over USB serial, and receives control commands from the display.

---

## Quick Start

1. Flash the ESP32 using the [browser firmware installer](https://infinitetimez.github.io/ESP32-Media-Player/).
2. Download the latest Windows companion from GitHub Releases.
3. Connect the display over USB.
4. Run the desktop companion.
5. Optionally enable Discord integration.

No Python or PlatformIO installation is required when using the prebuilt release files.

For installation details and troubleshooting, continue with the **[Quick Start Guide](docs/QUICK_START.md)**.

---

## Building the Hardware

The repository includes the enclosure files, wiring references, and documentation required to recreate the reference unit.

The reference build uses:

- ELECROW CrowPanel Advance 4.3" 800 × 480 ESP32 HMI Display
- EC11 rotary encoder
- 32 × 13 mm aluminum knob
- M3 heat-set inserts and screws
- Square magnets
- 22 AWG solid-core wire
- Printed display housing and stand

For exact parts, dimensions, and purchase links, see the **[Bill of Materials](hardware/BOM.md)**.

For connector trimming, heat-set inserts, magnet installation, encoder wiring, and final assembly, continue with the **[Assembly Guide](hardware/ASSEMBLY.md)**.

---

## EC11 Encoder Wiring

| EC11 Pin / Function | ESP32 Connection |
| --- | --- |
| A | GPIO 19 |
| B | GPIO 20 |
| C / Common | GND |
| Push switch | GPIO 8 |
| Remaining switch terminal | GND |

The push-switch terminals are interchangeable. If clockwise rotation decreases volume instead of increasing it, swap **A** and **B**.

See the labeled references in [`hardware/images/wiring/`](hardware/images/wiring/) or the **[Assembly Guide](hardware/ASSEMBLY.md)** for full wiring notes.

---

## Serial Protocol

Desktop-to-ESP32 application traffic uses a framed binary protocol:

```text
A5 5A | TYPE | uint32 LE LENGTH | PAYLOAD
```

| Type | Payload | Purpose |
| --- | --- | --- |
| `R` | None | ESP32 readiness |
| `T` | JSON | Track/media state |
| `I` | JPEG | Album artwork |
| `U` | JSON | Discord voice roster |
| `A` | Index + JPEG | Discord user avatar |
| `D` | State bytes | Discord mute/deafen state |
| `N` | Text | Discord voice-channel name |

The firmware parser can recover from incomplete or corrupted data by searching for the next valid frame boundary. Large image packets are sent in smaller chunks to avoid overwhelming the ESP32 receive path.

---

## Project Structure

```text
ESP32-Media-Player/
├── desktop-companion/
│   ├── assets/
│   ├── main.py
│   ├── hardware.py
│   ├── media.py
│   ├── network.py
│   ├── discord_client.py
│   ├── config.py
│   ├── requirements.txt
│   └── PyInstaller spec files
│
├── firmware/
│   ├── boards/
│   ├── include/
│   ├── src/
│   │   ├── components/
│   │   ├── images/
│   │   ├── screens/
│   │   ├── main.cpp
│   │   ├── serial_protocol.cpp
│   │   ├── media_display.cpp
│   │   ├── discord_ui.cpp
│   │   └── music_player_logic.c
│   ├── partitions.csv
│   ├── platformio.ini
│   ├── sdkconfig.defaults
|   └── sdkconfig.defaults.esp32s3
│
├── hardware/
│   ├── 3d-models/
│   ├── images/
│   ├── ASSEMBLY.md
│   └── BOM.md
│
├── docs/
│   ├── firmware/
│   ├── index.html
│   ├── manifest.json
│   └── QUICK_START.md
│
├── README.md
├── LICENSE
└── .gitignore
```

---

## Running From Source

### Desktop Companion

Requirements:

- Windows
- Python 3
- Discord desktop client if using Discord integration

```powershell
cd desktop-companion
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
python main.py
```

### ESP32 Firmware

The firmware is built with PlatformIO:

```bash
cd firmware
pio run
pio run --target upload
```

Serial communication uses **230400 baud**.

---

## Discord Setup

Discord integration is optional.

1. Create an application in the [Discord Developer Portal](https://discord.com/developers/applications).
2. Copy the **Application ID** and **Client Secret**.
3. Add this OAuth2 redirect URI:

```text
http://127.0.0.1
```

4. Enable **Discord Integration** from the desktop companion's system-tray menu.
5. Enter the requested credentials and restart the companion.

> Keep the Client Secret private. Do not commit it to GitHub or include it in screenshots.

---

## Releases

Prebuilt Windows builds and merged ESP32 firmware images are available on the GitHub Releases page.

The ESP32 can also be flashed directly using the [browser firmware installer](https://infinitetimez.github.io/ESP32-Media-Player/).

---

## Known Limitations

- The desktop companion currently supports Windows only.
- Firmware configuration targets the reference ELECROW display hardware.
- The Discord interface displays up to 15 visible voice-channel users.
- Album artwork depends on thumbnail data supplied by the active Windows media application.

---

## Project Status

This was primarily a summer project, and since I'm a student, updates may be inconsistent depending on how busy I am.

If you run into an issue, feel free to open one and I'll get to it when I can.

---

## License

Licensed under the GNU General Public License v3.0. See [`LICENSE`](LICENSE) for details.
