# Bill of Materials

This is the hardware used for the reference ESP32 Media Player build.

The linked parts are the exact products used where possible. Equivalent components with matching dimensions/specifications should also work. Product listings may change or become unavailable over time.

## Main Components

| Component | Qty. | Specification | Reference Link | Notes |
| --- | ---: | --- | --- | --- |
| ESP32 Touch Display | 1 | ELECROW CrowPanel Advance 4.3", 800×480 IPS touch display | [ELECROW](https://www.elecrow.com/crowpanel-advance-4-3-hmi-esp32-800x480-ai-display-ips-touch-artificial-intelligent-screen.html) | The original Amazon listing used for the build is no longer available. |
| Rotary Encoder | 1 | EC11 rotary encoder with push switch, 15 mm D-shaft | [Amazon](https://www.amazon.com/dp/B07D3D64X7) | Used for volume control, play/pause, and system mute. |
| Encoder Knob | 1 | 32×13 mm aluminum knob for a 6 mm shaft | [Amazon](https://www.amazon.com/dp/B07X7RDQ1W) | Fits the EC11 encoder used in the reference build. |
| M3 Heat-Set Inserts | 4 | M3 brass threaded heat-set inserts | [Amazon](https://www.amazon.com/dp/B0FWWW8VP1) | Installed at approximately 270 °C in the PETG-HF reference print. See assembly notes before installing. |
| M3×12 Screws | 4 | M3 Phillips machine screws, 12 mm length | [Amazon](https://www.amazon.com/dp/B0D1CRFR6Z) | Used with the M3 heat-set inserts. |
| Square Magnets | 2 | 1.26" × 1.26" square, 2 mm thick | [Amazon](https://www.amazon.com/dp/B0GQ47BZJS) | Orient the two magnets so they attract each other when the enclosure and stand are assembled. |
| Hookup Wire | As needed | 22 AWG solid-core wire | [Amazon](https://www.amazon.com/dp/B07TX6BX47) | Used for the rotary encoder and other internal wiring. |

## 3D-Printed Parts

Two printed parts are required:

| Part | Qty. | File |
| --- | ---: | --- |
| Display Housing | 1 | [`Display_Housing_Print.stl`](3d-models/Display_Housing_Print.stl) |
| Stand | 1 | [`Stand_Print.stl`](3d-models/Stand_Print.stl) |

The STL files are already oriented in the same orientation used for the reference prints.

### Printing Notes

The reference enclosure was printed using **PETG-HF**.

Other suitable 3D-printing materials should also work. The reference prints did **not require supports**, although support requirements may vary depending on the material, printer, and slicer settings being used.

## Additional Materials

### Magnet Adhesive

The reference magnets include double-sided adhesive.

If the included adhesive is unavailable or needs to be replaced, approximately **1 mm thick double-sided tape** can be used instead.

Make sure the magnets are oriented so they **attract each other** before permanently attaching them.

### Display Connector Clearance

The four beige connector housings on the CrowPanel display do not fit inside the printed enclosure at their full height.

For the reference build, the connector housings were carefully **cut/trimmed down to provide sufficient clearance inside the enclosure**.

More advanced builders may instead:

1. Desolder the connector housings from the display PCB.
2. Solder the required wires directly to the board.

Direct soldering can provide a cleaner and lower-profile installation, but trimming the existing housings is the easier option for most builders.

## Rotary Encoder Wiring

The EC11 rotary encoder connects to the ESP32 as follows:

| EC11 Pin | ESP32 |
| --- | --- |
| A | GPIO 19 |
| B | GPIO 20 |
| C / Common | GND |
| Push Switch | GPIO 8 |
| Other Push-Switch Terminal | GND |

The two push-switch terminals are not polarized, so either switch terminal can be connected to GPIO 8 while the other is connected to ground.

If the encoder rotates in the opposite direction from expected, swap the **A** and **B** connections.

See the wiring images in [`images/wiring/`](images/wiring/) for the labeled board and encoder pinouts.