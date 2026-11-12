# Midea XYE HVAC

<p align="center"><img src="docs/img/vibe-coded.svg" alt="VIBE CODED TO THE MAX" width="720"></p>

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
  device page; the unit then regulates on that room temperature. Leave it at **System/Thermostat Default** (or clear it) to keep the thermostat / unit sensor.
* **Aux heat** switch and **Self Clean** button (experimental).
* **Shared-bus mode**: works alongside a wired thermostat that also sits on XYE.
* **Safety**: commands are only sent when you change something, built from the
  unit's last reported state; nothing at boot; **XYE Control Enabled** turns it into
  a read-only monitor.
* One device file for every unit: `name_add_mac_suffix` gives each board a unique name.

## Quick start

1. Wire the ESP32 + RS-485 transceiver (tested with [this TTL-to-RS485 module](https://www.amazon.com/dp/B0BKG7SC54)) to X / Y / E ([docs/hardware.md](docs/hardware.md)).
   Want a tidy board? ken's PCB is on [OSHWLab](https://oshwlab.com/ken.mickens_1966/esp32-mod-bus) ([details](hardware/pcb/README.md)), with spare I/O broken out for add-on sensors or relays.
2. Copy [`examples/device-example.yaml`](examples/device-example.yaml) into your ESPHome
   config folder. Adjust `esp32:` for your board and the `xye_rx_pin` / `xye_tx_pin`
   substitutions if needed.
3. Add `wifi_ssid`, `wifi_password` and `ota_password` to `secrets.yaml` (see [Secrets](#secrets) below).
4. Install. The device appears as `midea-xye-hvac-xxxxxx`; adopt it in Home
   Assistant and rename it there.
5. In HA: **Settings → Devices & services → ESPHome → device → Configure →
   Allow the device to perform Home Assistant actions** (needed for follow-me).

The example pulls the component and the core package straight from this repo, so
updates arrive on your next install.

> **Running multiple units?** Check out my other project,
> [ESPHOME-Device-Scan](https://github.com/deathvenom-qvp/ESPHOME-Device-Scan).

## Secrets

The example YAML never contains passwords. Lines like `password: !secret wifi_password`
tell ESPHome to look the value up in **`secrets.yaml`**, a file that sits next to your
device YAMLs in the ESPHome config folder (`/config/esphome/secrets.yaml` on the Home
Assistant add-on). In the ESPHome dashboard, open it with the **Secrets** button at the
top right. Create it if it doesn't exist yet.

Add these three entries, with your own values:

```yaml
# secrets.yaml
wifi_ssid: "YourNetworkName"
wifi_password: "YourWiFiPassword"
ota_password: "pick-any-long-random-string"
```

| Secret | Used for |
|---|---|
| `wifi_ssid` | The name of the Wi-Fi network the ESP32 joins. It is case-sensitive, and the ESP32 needs a 2.4 GHz network. |
| `wifi_password` | That network's password. It is also the password of the fallback hotspot **"Midea XYE HVAC Setup"**, which the board opens if it can't join Wi-Fi. |
| `ota_password` | Protects wireless (OTA) firmware updates so nobody else on your network can flash the board. Any string works. Keep it, because later updates must use the same one. |

Notes:

* The names must match exactly. ESPHome stops with `Secret 'xxx' not defined` if one is missing.
* Every device in the folder shares one `secrets.yaml`, so set these once and reuse them.
* If your existing devices use different secret names (for example `ota_pin`), either add
  the three names above or change the `!secret` names in your copy of the example.
* Never commit `secrets.yaml` to a public repo. This repo's `.gitignore` already excludes it.
* Optional: to encrypt the Home Assistant connection, add `encryption: key: !secret api_key`
  under `api:` and put a key from
  [esphome.io/components/api](https://esphome.io/components/api/) in `secrets.yaml` as `api_key`.

## Repository layout

| Path | Contents |
|---|---|
| `components/xye_climate/` | ESPHome external component (C++ / Python) |
| `midea-xye-hvac.yaml` | Core package, pulled in automatically. **Don't paste this one into ESPHome.** |
| `examples/device-example.yaml` | **The file you paste into ESPHome** (one per unit) |
| `docs/protocol.md` | XYE frame formats and byte maps |
| `docs/hardware.md` | Parts and wiring |
| `hardware/pcb/` | PCB project details ([OSHWLab](https://oshwlab.com/ken.mickens_1966/esp32-mod-bus)) |
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
* Home Assistant community threads: [Midea A/C via local XYE](https://community.home-assistant.io/t/midea-a-c-via-local-xye/857679),
  [Full telemetry for Midea-based mini-splits / ducted units](https://community.home-assistant.io/t/full-telemetry-for-midea-based-mini-splits-centrally-ducted-units/1015912),
  [Reverse engineering Senville/Midea S1S2 bus](https://community.home-assistant.io/t/reverse-engineering-senville-midea-scomms/992233),
  [Hyundai (Midea clone) via RS485](https://community.home-assistant.io/t/troubleshooting-hyundai-heat-pump-midea-clone-integration-into-home-assistant-via-rs485-tcp/660986) and
  [Midea-branded ACs with ESPHome](https://community.home-assistant.io/t/midea-branded-ac-s-with-esphome-no-cloud/265236)
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
