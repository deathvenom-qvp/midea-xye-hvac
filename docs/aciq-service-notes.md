# ACiQ service manual notes (relevant to the XYE ESP)

Sources (uploaded 2026-11-12):
- ACiQ-xxCD-HH-MC High-Static Concealed Duct air handler, service manual 01-06-25
- ES/ACiQ-xxZ-HH-MxB multi-zone condenser, service manual 05-13-24
- ACiQ-Kxx mini-split wall units, service manual 08-21-24

## Air handler board (CD-HH-MC)
- **"TO CCM - Comm.Bus Central Controller"** terminal = XYE. Central-controller address set by
  micro-switch **S1 + dial SW8, range 00-63**. Matters if several units share one XYE bus.
- **HA/HB** = wired controller bus (no polarity). One controller can run up to 16 units, or 2 controllers one unit.
- **CN40** on this board is the **electric auxiliary heat module** (converter board), not an XYE+ port.
- **SW1**: cooling+heating / cooling-only; twin master/slave.
- **SW2**: anti-cold-air indoor fan stop temp (TEL0: 24 / 15 / 8 °C); heating temperature compensation (6/2/4 °C).
- **SW8**: auto-restart. **ENC1**: capacity (AUTO or 5K..60K, glued).
- **CN33** alarm relay output (dry contact, <=24 V 0.5 A, closes on fault). **CN23** remote ON/OFF (remove J7; 12 V DC).
- Static pressure SP1..SP8 or constant air volume (engineering channel 8).

### Aux heat logic
Turns off when T1 >= target, T2 >= 54 °C, indoor fan off, defrosting, system off, or a fault.
If turned on with the unit off, indoor fan runs 30 s at low. **With aux heat on there is no anti-cold-air function.**

## Codes seen by the ESP
- XYE "prot 0002" while heating with fan held off = **PH09 Anti-cold air in heating mode** (confirmed by this manual's list).

## Engineering mode (IR remote: Power+Fan 7 s, channels 0-30; values shown on unit display only)
0 error code, 1 T1, 2 T2, 3 T3, 4 T4 ambient, 5 TP discharge, 6 target freq, 7 actual freq, 8 current (3.2 A = 3),
9 AC voltage, 11 indoor mode (0 off,1 cool,2 heat,3 fan,4 dry,5 auto,7 defrost,12 active clean), 12 outdoor fan/8,
13 EXV steps/8, 14 indoor fan/8, 15 humidity, 16 set temp incl. compensation, 20 indoor target freq, 30 static pressure.
Advanced settings: 1 auto-restart, 2 fan at setpoint, 3 mode lock, 4/5 min/max setpoint, 8 static pressure / CAV,
11/12 min freq cool/heat, 15 forced-operation freq, 19/20 max freq cool/heat, 21/22 temp compensation,
23-26 fan speed trims, 28 anti-cold-air stop-fan temp (16-28 °C).

## Outdoor (multi-zone) point check: SW1 on the outdoor PCB, LED display
Presses: 1 # indoor units, 2 ODU mode, 3-7 IDU A-E capacity, 8-12 demand codes, 13-16 frequencies,
17-21 T2B per IDU, 22-26 T1 per IDU, 27-31 T2 per IDU, 32 T3, 33 T4, 34 TP, 35/36 current/voltage AD,
37-41 EXV per IDU (x4), 42 frequency-limit bits.
The inverter test tool (Carrier DLS / Dr Smart) plugs into the outdoor "detection cable" connector.

## Error / protection codes (indoor display, CD-HH-MC)
EH00 IDU EEPROM | EH0A IDU EEPROM param | EH03 IDU fan speed | EH0E water level | EH12 twin unit fault |
EH60 T1 sensor | EH61 T2 sensor | EHC1/EHC2/EHC3 refrigerant sensor | EH3A/EH3_ ext fan DC bus |
EL01 IDU-ODU comms | EL0C low refrigerant | EL11 twin comms | EC07 ODU fan speed | EC51 ODU EEPROM |
EC52 T3 | EC53 T4 | EC54 TP | EC55 IGBT temp sensor | EC56 T2B | PC00 IPM | PC01 voltage | PC02 comp top/IPM temp |
PC03 pressure | PC04 inverter drive | PC06 discharge temp | PC08 outdoor current | PC0A condenser high temp |
PC0L low ambient | PC10/PC11/PC12 low/over/DC voltage | PC30/PC31 high/low pressure | PC40-PC49 compressor drive faults |
PH09 anti-cold air | PH90 evaporator too hot | PH91 evaporator too cold | LH00/LC01/LC02/LC03/LC05/LC06/LC30/LC31 frequency limits |
EH 3 wired controller comms (KJR-120X/M/N).
XYE b22/b23 (error) and b24/b25 (protection) bit positions for these codes are not documented; map them as they appear.
