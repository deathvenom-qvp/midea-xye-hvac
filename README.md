# Midea XYE HVAC

> **vibe coded to the max**

Fully local Home Assistant control of Midea-family heat pumps, air handlers and
mini-splits (Midea, ACiQ, Carrier / Bryant rebadges, and similar) through the
indoor unit's **XYE / CCM RS-485 terminals**, using an ESP32 and ESPHome.
No cloud, no Wi-Fi dongle.

## What you get

* **Climate entity**: off, heat, cool, heat/cool (auto), dry, fan only; fan auto / low / medium / high; setpoint in °F.
* **Sensors**: return air (T1), indoor coil (T2A), outdoor coil (T3).
* **Status**: compressor running, sleep, self-clean running, protection, unit error,
  plain-text **Unit Status** (e.g. "Anti-cold-air (heat standby)").
* **Follow-me**: type any HA temperature entity into **Follow Me Sensor** on the
  device page; the unit then regulates on that room temperature. Blank = off.
* **Aux heat** switch and **Self Clean** button (experimental).
* **Shared-bus mode**: works alongside a wired thermostat that also sits on XYE.
* **Safety**: commands are only sent when you change something, built from the
  unit's last reported state; nothing at boot; **XYE Control Enabled** turns it into
  a read-only monitor.
* One device file for every unit: `name_add_mac_suffix` gives each board a unique name.

## Quick start

1. Wire the ESP32 + RS-485 transceiver to X / Y / E ([docs/hardware.md](docs/hardware.md)).
2. Copy [`examples/midea-xye-hvac.yaml`](examples/midea-xye-hvac.yaml) into your ESPHome
   config folder. Adjust `esp32:` for your board and the `xye_rx_pin` / `xye_tx_pin`
   substitutions if needed.
3. Add `wifi_ssid`, `wifi_password` and `ota_password` to `secrets.yaml`.
4. Install. The device appears as `midea-xye-hvac-xxxxxx`; adopt it in Home
   Assistant and rename it there.
5. In HA: **Settings → Devices & services → ESPHome → device → Configure →
   Allow the device to perform Home Assistant actions** (needed for follow-me).

The example pulls the component and the core package straight from this repo, so
updates arrive on your next install.

## Repository layout

| Path | Contents |
|---|---|
| `components/xye_climate/` | ESPHome external component (C++ / Python) |
| `midea-xye-hvac.yaml` | Core package: climate, sensors, switches, buttons |
| `examples/` | Device file to copy |
| `docs/protocol.md` | XYE frame formats and byte maps |
| `docs/hardware.md` | Parts and wiring |
| `docs/aciq-service-notes.md` | DIP switches, error codes, service modes from ACiQ manuals |
| `docs/capture-workflow.md` | How to map an unknown unit |
| `tools/` | Bus sniffer firmware, HA sweep script, capture analyser |

## Limits

XYE carries mode, fan, setpoint, three temperatures and status flags. Compressor
frequency, current, power, voltage, discharge temperature, fan RPM, humidity and
outdoor ambient are **not** on XYE (they travel over the Wi-Fi dongle / HA-HB bus).
The indoor coil reading is clamped at 99 °F by the unit.

Tested on an ACiQ (Midea) high-static ducted air handler set to °F. Other units
should work; if yours reports something odd, see
[docs/capture-workflow.md](docs/capture-workflow.md).

## Credits

Built by [deathvenom-qvp](https://github.com/deathvenom-qvp) together with
**Claude** (Anthropic), via Claude Code: protocol analysis, the ESPHome
component, tools and docs.

Standing on the shoulders of:

* [wtahler/esphome-mideaXYE-rs485](https://github.com/wtahler/esphome-mideaXYE-rs485): the ESPHome XYE firmware this project grew out of
* [Bunicutz/ESP32_Midea_RS485](https://github.com/Bunicutz/ESP32_Midea_RS485) and
  [Chron-O-John/esphome-mideaXYE-rs485-esp-idf-manual-flow-control](https://github.com/Chron-O-John/esphome-mideaXYE-rs485-esp-idf-manual-flow-control): C3 command layout, confirmed on a Carrier 40MBAB
* [mdrobnak/esphome](https://github.com/mdrobnak/esphome) and [HomeOps/ESPHome-Midea-XYE](https://github.com/HomeOps/ESPHome-Midea-XYE): XYE climate work, C4 / C6 frames
* [Codeberg XYE protocol notes](https://codeberg.org/xye/xye): frame and byte reference
* Home Assistant community thread [Midea A/C via local XYE](https://community.home-assistant.io/t/midea-a-c-via-local-xye/857679)
* [ALLTERCO/shelly-script-examples](https://github.com/ALLTERCO/shelly-script-examples) (Midea XYE script): reply byte map
* [fmck3516/midea-telemetry-esphome](https://github.com/fmck3516/midea-telemetry-esphome): outdoor-unit test-port telemetry
* [fabianschwamborn/HVAC-shark](https://github.com/fabianschwamborn/HVAC-shark): Midea bus sniffing notes
* ESPHome's [Midea climate component](https://esphome.io/components/climate/midea/) and the midea-msmart / midea-ac-py projects: Wi-Fi dongle protocol
* ACiQ service manuals (CD-HH-MC ducted, multi-zone condenser, K-series): DIP switches, error codes, service modes

## License

[CC BY-NC-SA 4.0](LICENSE): free for anyone to use, share and modify for
**non-commercial** purposes. Changes you share must credit this project and be
released under the same license, so it stays open. Selling it, or using it in
a commercial product or service, is not allowed without permission from the
copyright holder.
