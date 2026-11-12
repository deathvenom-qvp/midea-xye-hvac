# Mapping a new unit

Use this when a unit reports something the component doesn't understand.

1. Flash `tools/sniffer/xye-sniffer.yaml` (+ `xye_sniffer.h`). It frames every
   XYE byte and sends each frame to Home Assistant as an `esphome.xye_frame`
   event. In HA allow the device to perform actions (ESPHome integration →
   Configure).
2. Add `tools/homeassistant/xye_catalog.yaml` as an HA package and set up the
   File integration notify entity writing `/config/xye/capture.log`. Edit the
   three entity ids at the top.
3. Run `script.xye_catalog_run`. It steps the unit through every mode,
   setpoint, fan, swing, preset and switch the Wi-Fi integration exposes,
   logging XYE frames and HA state side by side. It restores the original
   state at the end (`script.xye_catalog_restore` if interrupted).
4. `python3 tools/analysis/xye_analyze.py capture.log -o report` writes
   `report.md` (per-step byte changes, setting → byte mapping, temperature
   fits), `steps.csv` and `frames.csv`.

The sniffer only ever transmits the read-only C0 query, and only while
"XYE Query Poll" is on.
