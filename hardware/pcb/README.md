# Midea XYE HVAC PCB

* **Project page (OSHWLab):** https://oshwlab.com/ken.mickens_1966/esp32-mod-bus
* **Open in EasyEDA Pro:** https://pro.easyeda.com/editor#id=e4bf7a95361d40da907ab74b8c0c1d2d

# Project introduction
ESP32 + RS-485 board for fully local Home Assistant control of Midea/ACiQ HVAC over the XYE bus. ESPHome, no cloud. Vibe coded to the max.

# Project function
This board connects an ESP32-S3 to the XYE / CCM RS-485 terminals (X, Y, E) on the indoor unit of Midea-family heat pumps, air handlers and mini-splits (Midea, ACiQ, Carrier / Bryant rebadges and similar). It runs ESPHome and appears in Home Assistant as a native climate device, with no cloud account, Wi-Fi dongle or vendor app.

Typical uses:
* Replace or supplement the cloud app with local Home Assistant automations.
* Monitor ducted air handlers, including units whose wall thermostat talks on the HA/HB bus or shares the XYE bus.
* Feed a room temperature sensor from Home Assistant to the unit ("Follow Me") so it regulates on the room you care about.

# Project Parameters
* **Controller:** ESP32-S3 DevKitC-1 (Wi-Fi, OTA updates, ESPHome web server).
* **Bus interface:** TTL-to-RS485 module with MAX485 and automatic direction control. No DE/RE pin needed.
* **UART:** 4800 baud, 8N1. RX on GPIO16, TX on GPIO17 (changeable in the YAML).
* **Unit connection:** X → A, Y → B, E → GND (signal reference).
* **Control:** mode (off / cool / heat / fan / dry / auto), setpoint in °F, fan speed (auto / low / medium / high).
* **Readings:** return air (T1), indoor coil (T2A / T2B), outdoor coil (T3), compressor state, unit status text, error and protection codes, and anti-cold-air (heat standby).
* **Follow Me:** sends any Home Assistant temperature sensor to the unit. If left blank, it falls back to the thermostat or the unit's own sensor.
* **Shared bus:** detects a wired thermostat on the same XYE terminals and polls only when the bus is quiet.
* **Expansion I/O:** spare ESP32 GPIO is broken out on headers for add-on devices such as I2C or DHT sensors, relays, leak detectors or status LEDs, added with normal ESPHome YAML.
* **Listen-only option:** a switch in Home Assistant (or removing the TX pin) makes it a read-only monitor.

# Principle analysis (Hardware description)
The board has three parts: power, the main controller, and the RS-485 interface.

* **Power:** the ESP32-S3 module is powered over 5 V / USB, and its 3.3 V rail powers the RS-485 transceiver so logic levels match.
* **Main controller:** the ESP32-S3 runs ESPHome with a custom `xye_climate` component. It polls the indoor unit with 16-byte query frames (C0 status, C4 extended status), parses the 32-byte replies into Home Assistant entities, and sends C3 set commands and C6 Follow Me frames.
* **Expansion headers:** unused ESP32 GPIO (plus power and ground) is brought out so you can add auxiliary devices without modifying the board.
* **RS-485 interface:** the MAX485 module converts the ESP's UART to the differential XYE bus. Its automatic direction control switches between transmit and receive without a GPIO.

# Software code
Firmware, docs, and protocol notes are on GitHub. ESPHome pulls the component straight from the repo, so you only need one YAML file:

[https://github.com/deathvenom-qvp/midea-xye-hvac](https://github.com/deathvenom-qvp/midea-xye-hvac)

```yaml
substitutions:
  name: midea-xye-hvac
  friendly_name: Midea XYE HVAC

packages:
  midea_xye_hvac:
    url: https://github.com/deathvenom-qvp/midea-xye-hvac
    files: [midea-xye-hvac.yaml]
    ref: main
```

A full example device file is in `examples/midea-xye-hvac.yaml`.

# Announcements
* Connect only to **X / Y / E**. **Never** connect to **HA/HB** (wired controller bus, about 18 V) or **S / S1 / S2 / N** (indoor-outdoor link, often at mains potential).
* Turn off power at the breaker before opening the unit.
* E is a signal reference, not mains earth.
* Use one 120 Ω termination per bus, not one per device. Many of these RS-485 modules already have one fitted.
* If you swap the module for a bare MAX485 with DE/RE pins, add `flow_control_pin:` to the `uart:` block.
* Several units on one bus need different addresses (DIP S1 + SW8 on ACiQ boards).
* License: CC BY-NC-SA 4.0. Free to build, share, and modify, but not for commercial use.

# Assembling processes
1. Solder the ESP32-S3 DevKitC-1 headers and the RS-485 module to the board.
2. Flash the firmware over USB from the ESPHome dashboard using the example YAML.
3. With the HVAC powered off, run a 3-wire cable from the board's A / B / GND to the unit's X / Y / E terminals.
4. Power the board, then restore HVAC power. The device shows up in Home Assistant for adoption.
5. Check the logs for valid replies (status and setpoint), then turn on control.

# Finished product display
(Add photos of the assembled board and the installed unit here.)
