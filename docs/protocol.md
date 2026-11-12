# XYE protocol notes

XYE is the RS-485 "central controller" (CCM) bus on Midea-family indoor units.
Terminals are labelled X / Y / E (or "TO CCM"). **4800 baud, 8N1**, half duplex.
The indoor unit only talks when a master (central controller, wired thermostat,
or this ESP) sends it a command.

Everything below was confirmed on an ACiQ (Midea) ducted air handler set to °F,
unless marked *inferred*.

## Framing

* Command (master → unit): 16 bytes. Reply (unit → master): 32 bytes.
* Start `0xAA`, end `0x55`.
* Checksum (second-to-last byte) = `0xFF − (sum of every other byte) & 0xFF`.
* Command byte 13 = `0xFF − command` (C0 → 3F, C3 → 3C, C4 → 3B, C6 → 39, CC → 33).
* Byte 2 of a reply is `0x80`; byte 1 echoes the command.
* Byte 3 is the destination address (00 here). The unit's address is set with
  DIP S1 + SW8 (00-63) when several units share one bus.

## Commands

| Cmd | Purpose | Example |
|---|---|---|
| C0 | Status query | `AA C0 00 00 80 00 00 00 00 00 00 00 00 3F 81 55` |
| C3 | Set mode / fan / setpoint / flags | `AA C3 00 00 80 00 84 80 44 00 00 00 00 3C 39 55` = heat, fan auto, 68 °F |
| C4 | Extended query (not on every board) | `AA C4 00 00 80 00 00 00 00 00 00 00 00 3B 81 55` |
| C6 | Follow-me room temperature | `AA C6 00 00 80 00 00 00 00 00 06 15 00 39 66 55` = start, 21 °C |
| CC / CD | Lock / unlock local controls (untested) | |

### C3 layout

| Byte | Meaning |
|---|---|
| 6 | Mode: 00 off, 81 fan, 82 dry, 84 heat, 88 cool, 91 auto, 01 self-clean (*inferred*) |
| 7 | Fan: 80 auto, 01 high, 02 medium, 04 low |
| 8 | Setpoint in whole °F (unit set to °F) |
| 9 | Mode flags (*inferred* from reply b20): bit 0 sleep, bit 1 aux heat |
| 13 | 3C |

On this unit the reply to a C3 already shows the new state. (Some Carrier
boards reportedly return the old state; poll again to be sure.)

### C6 follow-me

Byte 10: 06 start, 02 update, 04 stop. Byte 11: temperature (documented as whole °C).
Resend about every 60 s. Stop it to hand control back to the unit's own sensor.

## C0 / C3 reply (32 bytes)

| Byte | Meaning | Notes |
|---|---|---|
| 6, 7 | Fixed / capabilities | 40, 14 here |
| 8 | Mode | as C3 byte 6, bit 7 = on. 10 = off after auto |
| 9 | Fan | bit 7 = auto; low bits = running speed (01 high, 02 medium, 04 low) |
| 10 | Setpoint °F | |
| 11 | T1 return air °F | |
| 12 | T2A indoor coil °F | **clamped at 99** |
| 13 | T2B | FF = not fitted |
| 14 | T3 outdoor coil °F | not outdoor ambient |
| 15 | Current | FF = not reported |
| 19 | Compressor running | 01 |
| 20 | Mode flags | bit 0 sleep, bit 1 aux heat |
| 21 | Op flags | bit 2 set in cool / dry |
| 22, 23 | Error | 00 = none |
| 24, 25 | Protection | b24 bit 1 = PH09 anti-cold-air in heating |
| 26 | CCM error | |
| 30 | Checksum | |

Not on XYE: eco/boost/away presets, swing, beeper, outdoor silent, compressor
Hz / current / power / voltage, discharge temperature, fan RPM, humidity,
outdoor ambient. Those travel over the Wi-Fi dongle / HA-HB bus.

Fan speeds: the Wi-Fi app's Silent reads as Low and its High as Medium on XYE;
only Full shows as High.

Temperatures: a unit set to °C reports `(raw − 0x28) / 2` °C instead
(community docs, not tested here).

## Shared bus

If a wired thermostat also polls XYE, the component detects its 16-byte
commands, stops its own polling, stays in sync from the replies, and only
transmits after 250 ms of silence. Follow-me should be left blank on such
units: the thermostat sends its own.

## References

* Codeberg/mdrobnak XYE notes and ESPHome forks (mdrobnak/esphome, HomeOps/ESPHome-Midea-XYE)
* Bunicutz/ESP32_Midea_RS485 and Chron-O-John's ESP-IDF fork (Carrier 40MBAB)
* Home Assistant community thread "Midea A/C via local XYE"
