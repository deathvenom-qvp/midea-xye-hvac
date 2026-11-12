# Hardware and wiring

## Parts

* ESP32 / ESP32-S3 board (any ESPHome-supported ESP32).
* RS-485 transceiver with **automatic direction control** (MAX13487 / "auto
  flow" TTL-485 modules). For a MAX485 with DE/RE, add
  `flow_control_pin:` to the `uart:` block.
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
