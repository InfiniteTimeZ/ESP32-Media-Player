# Bill of Materials

This page continues from the hardware overview in the main README and lists the parts used in the reference ESP32 Media Player build.

## Main Components

| Component | Qty. | Specification | Reference Link | Notes |
| --- | ---: | --- | --- | --- |
| ESP32 Touch Display | 1 | ELECROW CrowPanel Advance 4.3", 800 × 480 IPS touch display | [ELECROW](https://www.elecrow.com/crowpanel-advance-4-3-hmi-esp32-800x480-ai-display-ips-touch-artificial-intelligent-screen.html) | Reference display hardware |
| Rotary Encoder | 1 | EC11 with push switch, 15 mm D-shaft | [Amazon](https://www.amazon.com/dp/B07D3D64X7) | Volume, play/pause, and mute |
| Encoder Knob | 1 | 32 × 13 mm aluminum knob for 6 mm shaft | [Amazon](https://www.amazon.com/dp/B07X7RDQ1W) | Fits the reference EC11 |
| M3 Heat-Set Inserts | 4 | M3 brass threaded inserts | [Amazon](https://www.amazon.com/dp/B0FWWW8VP1) | Used with enclosure mounting points |
| M3 × 12 Screws | 4 | M3 Phillips machine screws | [Amazon](https://www.amazon.com/dp/B0D1CRFR6Z) | Used with the heat-set inserts |
| Square Magnets | 2 | 1.26" × 1.26", 2 mm thick | [Amazon](https://www.amazon.com/dp/B0GQ47BZJS) | Housing-to-stand attachment |
| Hookup Wire | As needed | 22 AWG solid-core wire | [Amazon](https://www.amazon.com/dp/B07TX6BX47) | Encoder and internal wiring |

## 3D-Printed Parts

| Part | Qty. | File |
| --- | ---: | --- |
| Display Housing | 1 | [`Display_Housing_Print.stl`](3d-models/Display_Housing_Print.stl) |
| Stand | 1 | [`Stand_Print.stl`](3d-models/Stand_Print.stl) |

Reference printing:

- **Material:** PETG-HF
- **Supports:** not required
- **Orientation:** STL files are already oriented as used for the reference prints

## Additional Notes

**Magnets:** The reference magnets include adhesive. Approximately **1 mm double-sided tape** can be used as a replacement. Confirm magnet orientation before permanent installation.

**Display connector clearance:** The four beige connector housings on the CrowPanel must be trimmed to fit inside the enclosure. Advanced builders can desolder the connectors and wire directly to the PCB instead.

For wiring, heat-set insert installation, connector trimming, and final assembly, continue with the **[Assembly Guide](ASSEMBLY.md)**.
