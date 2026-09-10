# Quick Start

This guide continues from the main README and is intended for users who already have the ESP32 Media Player assembled.

## 1. Flash the ESP32

The easiest installation method is the [browser firmware installer](https://infinitetimez.github.io/ESP32-Media-Player/).

Use a current desktop version of **Google Chrome** or **Microsoft Edge**.

1. Connect the CrowPanel to the computer over USB.
2. Close the desktop companion and any serial-monitoring software.
3. Open the browser installer.
4. Click **Install Firmware**.
5. Select the ESP32-S3 device.
6. Wait for installation to finish and allow the display to restart.

The installer flashes the complete merged firmware image automatically. No PlatformIO installation or manual flash offsets are required.

### Manual Firmware Installation

The merged firmware image is also available from GitHub Releases.

If flashing manually, write the merged image beginning at:

```text
0x0000
```

## 2. Install the Windows Companion

Download the latest Windows executable from GitHub Releases and run it.

The application runs from the Windows system tray.

## 3. Connect the Display

Connect the ESP32 Media Player over USB. The desktop companion automatically searches for the device and establishes the serial connection.

Once connected, the display should begin showing media information, album artwork, playback position, system volume, and the current time.

## 4. Controls

### Rotary Encoder

- Rotate — change Windows volume
- Single press — play/pause
- Double press — mute/unmute Windows audio

### Touchscreen

- Play/pause
- Previous/next track
- Seeking
- Supported Discord voice controls

## 5. Optional Discord Integration

Enable **Discord Integration** from the companion's system-tray menu and enter your Discord application credentials when prompted.

Discord must be running locally.

For Discord application setup, see the **Discord Setup** section in the main README.

## Troubleshooting

### Display is not connecting

- Confirm the ESP32 is connected over USB.
- Close any other program using the ESP32 serial port.
- Restart the desktop companion.
- Disconnect and reconnect the ESP32.

### Display connects but does not update

Restart the desktop companion and reconnect the display.

### Album artwork is stale

Some Windows media applications provide delayed thumbnail data. The companion retries artwork retrieval several times when a new track is detected, but stale artwork can still occur.

### Discord information is missing

- Confirm Discord is running.
- Confirm Discord integration is enabled.
- Verify the Discord application credentials.
- Restart the desktop companion after first-time setup.
