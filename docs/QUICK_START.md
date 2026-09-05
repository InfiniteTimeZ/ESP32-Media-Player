# Quick Start

This guide is for users who already have the ESP32 Media Player assembled.

## 1. Flash the ESP32

The easiest way to install the firmware is through the browser-based firmware installer.

- [*Open the ESP32 Media Player Firmware Installer*](https://infinitetimez.github.io/ESP32-Media-Player/)

### Browser Requirements

Use a current desktop version of:

- Google Chrome
- Microsoft Edge

The installer requires browser support for Web Serial.

### Installation

1. Connect the CrowPanel display to your computer over USB.
2. Close the ESP32 Media Sync desktop companion and any serial-monitoring software.
3. Open the browser firmware installer.
4. Click **Install Firmware**.
5. Select the ESP32-S3 device when prompted.
6. Wait for the firmware installation to complete.
7. Allow the display to restart.

The browser installer automatically flashes the complete merged firmware image at the correct address. No PlatformIO installation or manual flash offsets are required.

### Manual Firmware Installation

The merged firmware image is also available from the GitHub Releases page:

`ESP32-Media-Player-v0.1.0-firmware.bin`

If flashing manually, write the merged firmware image beginning at address:

```text
0x0000
```

## 2. Install the Windows Companion

Download the latest Windows executable from the GitHub Releases page.

Run:

ESP32-Media-Sync-v0.1.0-Windows-x64.exe

The application runs from the Windows system tray.

## 3. Connect the Display

Connect the ESP32 Media Player to the PC using USB.

The desktop companion will automatically search for the ESP32 and establish the serial connection.

Once connected, the display should begin showing:

- current track title
- artist
- album artwork
- playback position
- system volume
- current time

## 4. Controls

### Rotary Encoder

- Rotate → change Windows volume
- Single press → play/pause
- Double press → mute/unmute Windows audio

### Touchscreen

Use the on-screen controls for media playback, seeking, and other available functions.

## 5. Optional Discord Integration

Discord integration can be enabled from the desktop companion's system tray menu.

When enabled for the first time, you will be prompted for your Discord application credentials.

Discord integration provides:

- current voice channel
- voice-channel members
- user avatars
- mute/deafen state
- Discord voice controls

Discord must be running on the PC.

## 6. Troubleshooting

### Display is not connecting

- Make sure the ESP32 is connected by USB.
- Restart the Windows companion.
- Disconnect and reconnect the ESP32.
- Make sure no other application is using the ESP32 serial port.

### Display connects but does not update

Restart the desktop companion and reconnect the display.

### Album artwork does not update immediately

Windows media applications occasionally provide stale or delayed thumbnail data. The companion automatically retries artwork retrieval with the next song arrival.

### Discord information is not appearing

- Confirm Discord is running.
- Confirm Discord integration is enabled.
- Verify your Discord application credentials.
- First time setup? Restart the Media Software.