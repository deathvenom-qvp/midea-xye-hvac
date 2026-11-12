# Hardware and wiring

## Parts

* **Ready-made PCB:** [ESP32 Mod Bus on OSHWLab](https://oshwlab.com/ken.mickens_1966/esp32-mod-bus), editable in [EasyEDA Pro](https://pro.easyeda.com/editor#id=e4bf7a95361d40da907ab74b8c0c1d2d). See [hardware/pcb](../hardware/pcb/README.md). It also breaks out spare GPIO for add-on devices such as sensors or relays.

* ESP32 / ESP32-S3 board (any ESPHome-supported ESP32).
* RS-485 transceiver with **automatic direction control** (MAX13487 / "auto
  flow" TTL-485 modules). For a MAX485 with DE/RE, add
  `flow_control_pin:` to the `uart:` block.
* **Tested module:** the generic "TTL to RS485 to UART Converter Serial Adapter
  3.3V 5V Compatible with MAX485 Chip" board
  ([Amazon B0BKG7SC54](https://www.amazon.com/dp/B0BKG7SC54)). It has 4 pins
  on the TTL side (VCC, TXD, RXD, GND) and switches direction on its own, so no
  `flow_control_pin` is needed. Power it from the ESP's 3.3 V pin so its logic
  levels match the ESP32.
* 5-12 V supply, or power from the unit if it offers a low-voltage rail.

## Wiring

| Unit terminal | Transceiver |
|---|---|
| X | A |
| Y | B |
| E | GND (signal reference, not mains earth) |

Transceiver RX/TX → ESP pins set by `xye_rx_pin` / `xye_tx_pin` (default GPIO16 / GPIO17).

* **Never** connect to **HA/HB** (wired-controller bus, ~18 V) or **S/S1/S2/N**
  (indoor-outdoor link, often mains-referenced).
* One 120 Ω termination per bus, not per device.
* Several units on one bus need different addresses (DIP S1 + SW8 on ACiQ boards).

## Listen-only

Delete `tx_pin` (or turn off "XYE Control Enabled") for a guaranteed read-only monitor.
