# QTE Vision

QTE Vision is a Windows x64 application that detects the fixed QTE display, locks its Perfect area, tracks the red pointer, and sends the configured key once when the pointer enters the trigger area.

## Run

1. Set the monitor and QTE coordinates in `config.ini`.
2. Place `QTEVisionDebugger.exe` and `config.ini` in the same folder.
3. While a QTE is visible, press **F8** to capture its center key prompt as `reference.bmp`.
4. Press **F9** to save `qte_scope.bmp` and verify the configured circle.
5. Leave the program running. Automatic input is enabled while the program is active.

Close the status window to exit.

## Hotkeys

- **F7**: pause or resume capture, detection, and key input together.
- **F8**: save the center ROI as `reference.bmp`.
- **F9**: save the configured QTE scope as `qte_scope.bmp`.

These are the only global function keys. If Windows cannot register one because another program owns it, focus the QTE Vision window and press the key there.

## Trigger behavior

The program preserves the existing QTE appearance detection, Perfect recognition and lock, red-pointer detection, and QTE state machine. Once a Perfect area is locked, each valid pointer angle is checked against that area shifted by the fixed `trigger_offset_deg`. On the first entry, the state changes to `TRIGGERED` and the configured key is sent immediately with `SendInput`. The state machine prevents another key press during the same QTE and resets after QTE appearance disappears.

The offset shifts both Perfect boundaries by the same number of degrees. For example, a Perfect range of `245° ~ 256°` with `trigger_offset_deg=5.0` produces a trigger range of `250° ~ 261°`; the visually detected and locked Perfect range is unchanged. Positive values shift the numeric angle range forward, and negative values shift it backward. Range checks normalize angles, including ranges that cross `0°/360°`.

Angles use the detector's existing convention: `0°` right, `90°` up, `180°` left, and `270°` down.

## Configuration

`config.ini` contains the runtime settings:

- `[Capture] monitor`
- `[QTE] center_x`, `center_y`, `radius`, `ring_thickness`, and center ROI coordinates/dimensions
- `[Vision]` color thresholds, angular sampling, recognition thresholds, reference tolerance, and QTE match threshold
- `[Input] target_key` and `trigger_offset_deg` (`-180` through `180`; default `5.0`)

`reference.bmp` and `qte_scope.bmp` are saved beside `config.ini`. The application writes concise state and input error records to `qtevision.log` and keeps the most recent 500 lines.

## Build

Configure with a Windows x64 CMake generator and build either `Debug` or `Release`:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The executable is written to `build/bin/Release/QTEVisionDebugger.exe` (or `build/bin/Debug/QTEVisionDebugger.exe`).
