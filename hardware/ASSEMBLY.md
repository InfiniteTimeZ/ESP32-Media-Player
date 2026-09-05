# Hardware Assembly Guide

This guide is for builders recreating the reference ESP32 Media Player hardware.

## Tools

Recommended tools:

- Soldering iron
- Small Phillips screwdriver
- Flush cutters or hobby knife for trimming the display connector housings
- Heat-set insert tip or suitable soldering-iron tip
- Wire strippers
- Solder

## 1. Print the enclosure

Print both files from [`3d-models/`](3d-models/):

- `Display_Housing_Print.stl`
- `Stand_Print.stl`

### Reference print setup

- **Material used:** PETG-HF
- **Supports:** not required for the reference print
- **Orientation:** the STL files are already oriented the same way they were printed for the reference build

Other common 3D-printing materials should also work. Depending on the material, printer, and slicer settings, supports may be helpful even though they were not needed for the reference print.

## 2. Prepare the display board

### Important: connector clearance

The four beige connector housings on the display board do **not** fit inside the printed enclosure at their full original size.

For the reference build, all four beige connector housings must be carefully **cut/trimmed down** enough to clear the inside of the case.

#### Advanced alternative

Experienced builders can instead desolder the connector housings and solder the required wiring directly to the board. This provides a lower-profile installation, but carries a greater risk of damaging the display board and should only be attempted by someone comfortable with soldering/desoldering PCB connectors.

 
## 3. Installing the Heat-Set Inserts

The reference build uses **4 M3 heat-set inserts**.

For PETG-HF, approximately **270 °C** worked well for installing the inserts.

This temperature is only a reference point and may need to be adjusted slightly depending on the filament, soldering iron, and insert being used.

- If the iron is **too hot**, the insert can sink too quickly, making it easy to push the insert too far or accidentally melt nearby features.
- If the iron is **too cool**, excessive pressure may be required and the plastic surrounding the insert can begin to bulge or warp instead of melting cleanly around it.
- Apply light, steady downward pressure and allow the heat to soften the plastic rather than forcing the insert into place.
- Stop once the insert is seated flush with the intended surface.

Allow the insert and surrounding plastic to cool before installing a screw.

## 4. Install the magnets

The enclosure/stand uses **two** square magnets sized:

- **1.26 in × 1.26 in**
- **2 mm thick**

Reference product:
https://www.amazon.com/dp/B0GQ47BZJS?th=1

Install one magnet in each modeled magnet location and orient them so the two magnets **attract each other** when the housing and stand are brought together.

The reference magnets include double-sided adhesive. If that adhesive is unavailable or needs replacing, approximately **1 mm double-sided tape** should work as an alternative retention method.

Before pressing the magnets into place permanently, dry-fit both parts and verify attraction/orientation.

## 5. Wire the EC11 rotary encoder

The firmware expects the following GPIO assignments:

| Encoder pin/function | ESP32 |
| --- | --- |
| A | GPIO 19 |
| B | GPIO 20 |
| C / encoder common | GND |
| D or E / push switch | GPIO 8 |
| Remaining push-switch terminal | GND |

The D/E switch terminals are interchangeable because the push switch is simply a contact closure.

### CrowPanel GPIO reference

![CrowPanel GPIO reference](images/wiring/crowpanel-pinout.png)

### EC11 pin-label reference

![EC11 labeled pins](images/wiring/ec11-pin-labels.png)

> If turning the encoder clockwise lowers volume instead of raising it, swap the A and B connections.

## 6. Mount the encoder and knob

1. Install the EC11 encoder through the modeled opening.
2. Secure the encoder mechanically.
3. Confirm the shaft rotates freely and the push action is not obstructed by the housing.
4. Install the 32 × 13 mm aluminum knob onto the 6 mm shaft.

## 7. Mount the display

1. Confirm all four beige connector housings have enough enclosure clearance after trimming, or that direct wiring has been completed.
2. Position the display inside the housing without pinching any wires.
3. Route internal wiring away from the stand, screw bosses, and display edges.
4. Secure the display using the M3 hardware used by the enclosure design.

## 8. Final assembly

Before fully closing the case:

- Confirm the encoder rotates and presses freely.
- Confirm no wires are trapped between printed parts.
- Confirm the display USB connector remains accessible.
- Confirm the two magnets attract and the stand aligns correctly.
- Power the device and test the display before tightening the final screws.

## 9. Flash and test

After physical assembly, continue with [`../docs/QUICK_START.md`](../docs/QUICK_START.md).

Verify:

- display boots
- Windows companion connects
- album art and track information update
- encoder changes volume
- single press toggles play/pause
- double press toggles mute
- touchscreen controls work
- optional Discord UI updates correctly
