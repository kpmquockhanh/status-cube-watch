# Battery indicator in the top bar

## Goal
Show the Li-ion battery percentage on every card, so the cube's charge is visible at a glance. Purely on-device: the bridge, the payload contract, `cards.mjs` and `preview.html` do not change.

## Non-goals
- Charging bolt / charge state. The board docs list no charge-status GPIO (ETA6098 STAT is not routed to a documented pin). Revisit only if the schematic shows one.
- A dedicated battery card, low-battery sleep, or any power management.
- Mirroring in `preview.html` (it is device-local, like the Pomodoro card).

## Design

### Measurement: `firmware/src/battery.{h,cpp}`
- Reads `PIN_BAT_ADC` (GPIO1), `VBAT = VADC x 3` (200K/100K divider).
- `analogReadMilliVolts` averaged over 16 samples, sampled every ~5 s from the main loop (not every frame).
- `batteryPercent(mv)`: pure function, piecewise-linear Li-ion discharge table (4200 mV = 100, 3700 = ~50, 3300 = 0), clamped.
- Smoothing: pure `BatteryFilter` (exponential moving average on mV, first sample seeds it) so the number does not flicker with WiFi TX bursts.
- No cell: if smoothed mV is below 2500 (floating pin, USB-only), the cube can only be running from USB, so `pct = 100` and `onUsb = true`. No separate cable detection is needed or available. With a cell fitted, the percentage comes from the voltage (while USB charges, the cell sits near 4.2 V, so it reads ~100 anyway).
- API: `struct BatteryView { uint8_t pct; bool onUsb; }`, `batteryBegin()`, `batteryUpdate(uint32_t nowMs)`, `batteryView()`.

### Drawing: `ui.cpp`
- `drawTopBar` gains a `const BatteryView &bat` parameter (passed through `uiRender`).
- Layout, right side of the bar (Y = 24): `[label ........ battery pct%  | age dot]`. The battery glyph (~18x9 px outline + nub, fill proportional to pct) and `NN%` in `V_S12` sit left of the age readout, with a ~10 px gap.
- Colour via existing palette: green >= 40, amber 15..39, red < 15. The text and glyph fill share the colour; the outline is `DIM`.
- Always drawn. With `onUsb` (no cell) it shows `100%` in green, so the glyph is full.
- Left label gets a max width (bar width minus right cluster) and is truncated if longer.
- Applies to every card, including Pomodoro. Not drawn on the editor, portal, OTA or message screens.

### Simulator and tests
- `firmware/sim/battery_sim.cpp` replaces `battery.cpp`'s hardware read; level from env `CUBE_BATTERY` (percent, or `none` for the no-cell / USB case, which shows 100%), default 78.
- `cube-shot` picks it up, so `CUBE_BATTERY=10 ./build/cube-shot ...` shows red, `none` shows a full 100% indicator.
- Host tests (`make test`): `batteryPercent` table points and clamping, `BatteryFilter` seeding and convergence, the no-cell threshold (reads 100%).

### Docs
- README and CLAUDE.md: one line each noting the on-device battery indicator, `CUBE_BATTERY` in the simulator, and that it is not in the payload contract.

## Risks
- Voltage-to-percent is an estimate (load and temperature shift it); the table is approximate and the number is labelled `%` only.
- The ADC is not factory-calibrated beyond what `analogReadMilliVolts` provides; accept +-50 mV error.
- Right cluster width grows ~45 px; verify with `make shot` that nothing clips at the rounded corners for 3-digit ages (`99m`) and `OFFLINE`.
