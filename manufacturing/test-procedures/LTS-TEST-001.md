# LTS-TEST-001: LCU-100/LSO-100/LPB-100/LPI-100 Prototype Bring-Up and Verification Procedure

---

## 1. Document Control

| Field | Detail |
|---|---|
| Document Number | LTS-TEST-001 |
| Revision | A |
| Date | 2026-06-18 |
| Author | Lumina Engineering |
| Approved By | TBD |
| Status | DRAFT — FOR PROTOTYPE USE ONLY |
| Applicable Hardware | LCU-100 Rev A, LSO-100 Rev A, LPB-100 Rev A, LPI-100 Rev A |
| Related Documents | LTS-SCH-001 (Schematics), LTS-PCB-001 (PCB Layout), LTS-FW-001 (Firmware Specification) |

### 1.1 Revision History

| Rev | Date | Description | Author |
|---|---|---|---|
| A | 2026-06-18 | Initial release for prototype bring-up | Lumina Engineering |

---

## 2. Safety Warnings

> **WARNING — READ BEFORE PROCEEDING**

### 2.1 Electrical Hazards

- **High-Side Switch Outputs (LSO-100):** The BTS7030-2EPA high-side switches operate at the battery supply voltage (10.5–14.4V DC). Output connectors J3–J7 carry switched 12V DC capable of delivering up to 21A peak per channel. Ensure lamp load connectors are correctly mated before enabling any output channel. Never probe output pins with energised lamp loads attached unless using an insulated meter probe.
- **Battery Voltage:** The LPB-100 board connects directly to a 12V lead-acid or lithium battery via Anderson SB50 connector J1. Battery terminals can source hundreds of amps short-circuit current. Always fit the 40A MIDI fuse F1 before connecting any battery. Never short the battery terminals.
- **Reverse Polarity:** Although the design includes reverse polarity protection (MOSFET U9, PSMN1R4-40YLD), do not deliberately apply reverse polarity during testing. The protection device has a maximum reverse voltage rating of 40V.
- **Capacitor Discharge:** After removing supply power, wait a minimum of 30 seconds before probing internal rails. The bulk capacitors on the LPB-100 (C61–C65, 100µF 50V) retain charge after disconnection.

### 2.2 ESD Precautions

- All prototype PCBs must be handled in a designated ESD-safe area.
- Operators must wear a calibrated wrist strap (< 1MΩ measured to common) at all times when handling unpowered boards.
- Store all boards in anti-static bags when not in use.
- The STM32H743 (U1 LCU-100) and BQ76952 (U1 LPB-100) are sensitive to ESD damage above ±500V HBM. Exercise particular care with exposed SWD headers and connector pins.

### 2.3 Firmware and Safety

- Do not bypass the hardware INHIBIT signal during lamp output testing. The INHIBIT line must be asserted (active-low, driven by safety supervisor U2 STM32G071) before any output channel can be energised.
- Never attempt to simultaneously enable conflicting signal phases (e.g., two opposing GREEN aspects) via direct register writes during debug. The safety supervisor MCU enforces a conflict matrix and will assert INHIBIT within 30ms of detecting a conflict — but the test engineer must not rely solely on this protection.
- During short-circuit testing (Section 7), ensure personnel are clear of lamp connector J3–J7 before applying the test load. The BTS7030-2EPA current limit response time is < 1µs but fault clearing involves a brief overcurrent condition.

### 2.4 Bench Safety

- Bench power supply must be a current-limited regulated bench PSU (not a battery charger or unregulated supply). Set current limit to 2A before first power-on of any board.
- Increase current limit only after confirming no fault conditions exist.
- Keep a class-C fire extinguisher accessible in the test area.

---

## 3. Equipment Required

| Item | Description | Specification | Qty |
|---|---|---|---|
| PSU-1 | Bench Power Supply | 0–30V, 0–20A, CC/CV regulated, dual display | 1 |
| DMM-1 | Digital Multimeter | 4½ digit, 0.1% DC voltage accuracy, CAT III 600V | 1 |
| DMM-2 | Digital Multimeter | 4½ digit, true-RMS, milliamp range | 1 |
| OSC-1 | Oscilloscope | 200MHz bandwidth, 4-channel, 1GSa/s, 10Mpt memory | 1 |
| CAN-1 | CAN/CAN FD Analyser | PEAK PCAN-USB Pro or Kvaser Leaf Pro HS v2 | 1 |
| CLAMP-1 | AC/DC Current Clamp Meter | 1mA–60A DC, 1% accuracy | 1 |
| STLINK-1 | ST-Link V3 MINIE Debugger/Programmer | With SWD ribbon cable to 1.27mm 10-pin | 1 |
| PC-1 | Test Laptop or Workstation | Windows 10/11 or Ubuntu 22.04, USB 3.0, min 8GB RAM | 1 |
| SW-OPENOCD | OpenOCD v0.12.0 or later | STM32H7 and STM32G0 target support | 1 |
| SW-KICON | KiCon CAN Analyser Software | v3.2+ with CAN FD support | 1 |
| SW-STCUBEPROG | STM32CubeProgrammer v2.14+ | For SWD firmware flashing | 1 |
| LOAD-1 | Representative LED Lamp Load | 12V LED array, 1.2W per aspect (100mA), 3-aspect vehicle head | 1 set |
| LOAD-2 | Short-Circuit Test Load | 1Ω 10W wirewound resistor with banana plugs | 1 |
| LOAD-3 | Open-Circuit Test Fixture | Male Superseal 9-pin with all pins disconnected | 1 |
| TERM-1 | CAN Bus Terminator | 120Ω ±1% 1/4W SMD on SMA adapter | 2 |
| CAB-1 | SWD Programming Cable | 10-pin 1.27mm IDC ribbon, 150mm | 2 |
| CAB-2 | USB-C Cable | USB 3.1 Gen1, 500mm, Type-A to Type-C | 1 |
| CAB-3 | MiniFit Jr Harness | 8-pin MiniFit Jr male to female, 300mm, 16AWG | 1 |
| ANT-1 | Stub Antenna 868MHz | SMA male, 50Ω, for radio module test (if fitted) | 1 |

### 3.1 Software and Firmware Prerequisites

Before beginning this procedure, ensure the following firmware artefacts are available on the test PC:

| Artefact | Filename | Version |
|---|---|---|
| LCU Bootloader | `lcu100_bootloader_vX.X.X.hex` | ≥ 1.0.0 |
| LCU Application | `lcu100_app_vX.X.X.hex` | ≥ 1.0.0 |
| Safety MCU Firmware | `lcu100_safety_vX.X.X.hex` | ≥ 1.0.0 |
| OpenOCD Config | `lts_lcu100.cfg` | Rev A board config |
| CAN DBC File | `lumina_lts.dbc` | Rev A |
| Test Script | `lts_test001_auto.py` | Rev A |

---

## 4. Pre-Power Checks

These checks must be completed on every board before any power is applied. Record all results in the Test Record Sheet (Appendix A).

### 4.1 Visual Inspection

Inspect each PCB under magnification (10× minimum) for the following:

| Check | Criterion | Pass | Fail |
|---|---|---|---|
| VIS-01 | No visible solder bridges on QFN/LQFP pads — verify with 10× loupe | □ | □ |
| VIS-02 | All IC orientations correct — pin 1 marker aligned with PCB silkscreen dot | □ | □ |
| VIS-03 | No missing components (compare against BOM and assembly drawing) | □ | □ |
| VIS-04 | No tombstoned passives — all 0402/0805 components flat and correctly placed | □ | □ |
| VIS-05 | Connector J1–J8 (LCU) / J1–J7 (LSO) / J1–J3 (LPB) mechanically seated correctly | □ | □ |
| VIS-06 | Battery holder BT1 (LCU) — polarity marking correct, no deformation | □ | □ |
| VIS-07 | Crystal Y1 and Y2 (LCU) — no cracks in package or pad contamination | □ | □ |
| VIS-08 | M.2 socket J4 (LCU) — all contacts clean, no bent pins | □ | □ |
| VIS-09 | Anderson connectors J1/J2 (LPB) — correct gender, correct colour coding | □ | □ |
| VIS-10 | Fuse F1 (LPB) — 40A MIDI fuse installed (verify amperage printed on fuse) | □ | □ |

**Stop: Do not proceed if any VIS check fails. Rectify and re-inspect.**

### 4.2 Continuity and Isolation Checks (Unpowered)

Using DMM-1 in resistance/continuity mode, verify the following on each board before applying power:

#### LPB-100

| Check | Test Point A | Test Point B | Criterion | Result (Ω) | Pass | Fail |
|---|---|---|---|---|---|---|
| CONT-LPB-01 | J1 Pin 1 (+BATT) | GND plane | > 100Ω (no short) | | □ | □ |
| CONT-LPB-02 | J2 Pin 1 (+SOLAR) | GND plane | > 100Ω (no short) | | □ | □ |
| CONT-LPB-03 | J3 Pin 1 (+5V_OUT) | GND plane | > 10Ω (LDO present) | | □ | □ |
| CONT-LPB-04 | J3 Pin 3 (+12V_OUT) | GND plane | > 100Ω | | □ | □ |
| CONT-LPB-05 | TP_GND | PCB GND via | < 1Ω (common ground) | | □ | □ |

#### LCU-100

| Check | Test Point A | Test Point B | Criterion | Result (Ω) | Pass | Fail |
|---|---|---|---|---|---|---|
| CONT-LCU-01 | J1 Pin 1 (+5V) | GND | > 10Ω (LDO present) | | □ | □ |
| CONT-LCU-02 | J1 Pin 3 (+3.3V) | GND | > 10Ω (LDO present) | | □ | □ |
| CONT-LCU-03 | J5 VBUS (USB-C) | GND | > 100Ω | | □ | □ |
| CONT-LCU-04 | U1 pin 1 (VDDA) | GND | > 100Ω | | □ | □ |

#### LSO-100

| Check | Test Point A | Test Point B | Criterion | Result (Ω) | Pass | Fail |
|---|---|---|---|---|---|---|
| CONT-LSO-01 | J1 Pin 1 (+12V) | GND | > 100Ω | | □ | □ |
| CONT-LSO-02 | J3 Pin 1 (OUT_CH1) | GND | > 1kΩ (high-side switch off) | | □ | □ |
| CONT-LSO-03 | F1 IN | F1 OUT | < 0.5Ω (fuse continuity) | | □ | □ |

### 4.3 Connector Keying Verification

Verify that all connectors have correct keying such that incorrect mating is physically prevented:

| Check | Connector | Verification | Pass | Fail |
|---|---|---|---|---|
| KEY-01 | LPB J1 ↔ Anderson SB50 | SB50 mates only with correct polarity housing | □ | □ |
| KEY-02 | LPB J2 ↔ Anderson SB120 | SB120 physically larger than SB50 — cannot cross-mate | □ | □ |
| KEY-03 | LPB J3 ↔ LCU J1 | MiniFit Jr keying tab prevents reversal | □ | □ |
| KEY-04 | LSO J3–J6 ↔ Approach heads | Superseal 9-pin polarisation key correct | □ | □ |
| KEY-05 | LSO J7 ↔ Pedestrian head | Superseal 8-pin physically distinct from 9-pin | □ | □ |
| KEY-06 | LCU J8 SWD | 10-pin 1.27mm keyed header — key notch present | □ | □ |

---

## 5. LPB-100 Standalone Bring-Up

### 5.1 Initial Power Application

1. Set PSU-1 to 13.8V, current limit **2.0A**. Do not connect output yet.
2. Connect DMM-1 to LPB-100 TP_5V (positive) and TP_GND (negative).
3. Connect DMM-2 to LPB-100 TP_3V3 (positive) and TP_GND (negative).
4. Verify fuse F1 (40A) is installed.
5. Connect PSU-1 positive to J2 Pin 1 (solar/charger input) via test lead. Connect PSU-1 negative to J2 Pin 2 (GND). **Note:** Do not connect battery (J1) until J2 power is confirmed working.
6. Enable PSU-1 output.
7. Observe PSU-1 current reading for 5 seconds. If current exceeds **1.8A** before reaching 13.8V, immediately disable PSU and investigate (likely a solder bridge or incorrect component).

### 5.2 Supply Rail Voltage Checks

Record all measurements in the Test Record Sheet:

| Check | Test Point | Nominal | Acceptance Range | Measured (V) | Pass | Fail |
|---|---|---|---|---|---|---|
| PWR-LPB-01 | TP_5V (output from U6 buck) | 5.00V | 4.90–5.10V | | □ | □ |
| PWR-LPB-02 | TP_5V_LDO (output from U7 LDO) | 5.00V | 4.95–5.05V | | □ | □ |
| PWR-LPB-03 | TP_VBUS (J2 input) | 13.8V | 13.5–14.1V | | □ | □ |
| PWR-LPB-04 | TP_GND continuity across board | 0V | < 50mV | | □ | □ |
| PWR-LPB-05 | Quiescent current (PSU-1 display) | < 150mA | < 200mA total | | □ | □ |

### 5.3 BQ76952 Battery Monitor I2C Communication

Using the test PC running `lts_test001_auto.py`:

```
python lts_test001_auto.py --board LPB --test i2c_scan
```

Expected output:
```
I2C scan on BUS0:
  0x08 [BQ76952]  FOUND - Device ID: 0x60
  0x55 [PCT2075]  FOUND - Device ID: 0x91
  0x6B [BQ25798]  FOUND - Device ID: 0xD1
```

| Check | Criterion | Pass | Fail |
|---|---|---|---|
| I2C-LPB-01 | BQ76952 responds at address 0x08 | □ | □ |
| I2C-LPB-02 | BQ25798 responds at address 0x6B | □ | □ |
| I2C-LPB-03 | PCT2075 temperature sensor responds at 0x55 | □ | □ |
| I2C-LPB-04 | BQ28Z610 fuel gauge responds at 0x55 (SMBus) | □ | □ |

### 5.4 Battery Voltage Telemetry Accuracy

Connect a fully charged 12V test battery to J1 (Anderson SB50). Measure battery terminal voltage with DMM-1 directly at J1.

```
python lts_test001_auto.py --board LPB --test battery_voltage
```

| Check | Criterion | DMM Reading (V) | Firmware Reading (V) | Error (mV) | Pass | Fail |
|---|---|---|---|---|---|---|
| BAT-LPB-01 | Battery voltage within ±50mV of DMM | | | | □ | □ |
| BAT-LPB-02 | Battery current reading at 0A ± 10mA (no load) | | | | □ | □ |
| BAT-LPB-03 | Temperature reading within ±2°C of ambient | | | | □ | □ |

---

## 6. LCU-100 Bring-Up (LPB-100 Connected)

### 6.1 Firmware Flashing via SWD

Connect ST-Link V3 MINIE to LCU-100 J8 (10-pin 1.27mm SWD header) using CAB-1.

**Step 1: Flash bootloader to U1 (STM32H743)**

```bash
openocd -f lts_lcu100.cfg -c "program lcu100_bootloader_vX.X.X.hex verify reset exit"
```

Expected output:
```
** Programming Started **
** Programming Finished **
** Verify OK **
** Resetting Target **
```

**Step 2: Flash application firmware to U1**

```bash
openocd -f lts_lcu100.cfg -c "program lcu100_app_vX.X.X.hex verify reset exit"
```

**Step 3: Flash safety supervisor firmware to U2 (STM32G071)**

Change SWD mux to U2 (via board test point TP_SWD_SEL — drive high):

```bash
openocd -f lts_lcu100_safety.cfg -c "program lcu100_safety_vX.X.X.hex verify reset exit"
```

| Check | Criterion | Pass | Fail |
|---|---|---|---|
| FLASH-01 | Bootloader programs and verifies without error | □ | □ |
| FLASH-02 | Application firmware programs and verifies without error | □ | □ |
| FLASH-03 | Safety supervisor firmware programs and verifies without error | □ | □ |
| FLASH-04 | Board resets and enumerates on USB (LED D1 blinks at 1Hz) | □ | □ |

### 6.2 USB-C Service Port Enumeration

Connect CAB-2 (USB-C) from PC-1 to LCU-100 J5.

Expected Windows Device Manager entry: `Lumina LTS Service Port (COM x)` — USB VID 0x0483 PID 0x5740

Expected Linux: `/dev/ttyACM0` with descriptor `Lumina LTS-LCU100 Service Port`

| Check | Criterion | Pass | Fail |
|---|---|---|---|
| USB-01 | Device enumerates without errors | □ | □ |
| USB-02 | VID 0x0483 / PID 0x5740 reported correctly | □ | □ |
| USB-03 | Serial console responds to `\r` with firmware version string | □ | □ |

Open a terminal emulator (115200 8N1) and confirm firmware banner:

```
Lumina LTS-LCU100 v1.0.0 (Rev A)
Safety MCU: ACTIVE
FRAM: OK (4Mbit)
RTC: OK 2026-06-18 HH:MM:SS
CAN1: READY
CAN2: READY
RS485: READY
```

### 6.3 CAN Bus Heartbeat Verification

Connect CAN-1 (PCAN-USB) to LCU-100 J2 with 120Ω termination on both ends of CAN bus.

Open KiCon CAN Analyser Software. Configure:
- CAN FD, 500kbps nominal, 2Mbps data phase
- Load `lumina_lts.dbc`

Expected: CAN frame ID 0x010 (Heartbeat_LCU) at 100ms intervals, DLC=8

```
0x010  Heartbeat_LCU  [8]  HH MM SS xx xx xx xx xx
```

| Check | Criterion | Pass | Fail |
|---|---|---|---|
| CAN-01 | CAN ID 0x010 heartbeat received within 500ms of power-on | □ | □ |
| CAN-02 | Heartbeat interval: 95–105ms (< 5% jitter) | □ | □ |
| CAN-03 | Heartbeat sequence counter increments correctly | □ | □ |
| CAN-04 | No error frames on CAN bus | □ | □ |

### 6.4 RTC Verification

Via service port terminal:

```
> rtc_read
RTC: 2026-06-18 14:32:05  [VALID]
FRAM_BACKUP: 2026-06-18 14:32:05  [MATCH]
```

| Check | Criterion | Pass | Fail |
|---|---|---|---|
| RTC-01 | RTC returns valid date/time | □ | □ |
| RTC-02 | Remove and re-apply power — RTC time continues correctly (battery backup) | □ | □ |
| RTC-03 | RTC drift < 2 seconds over 1 hour powered observation | □ | □ |

### 6.5 FRAM Read/Write Test

Via service port:

```
> fram_test
Writing pattern 0xAA55 to all 524288 addresses...
Readback verification...
FRAM: PASS — 0 errors in 524288 bytes
```

| Check | Criterion | Pass | Fail |
|---|---|---|---|
| FRAM-01 | FRAM writes and reads 0xAA55 pattern with 0 errors | □ | □ |
| FRAM-02 | FRAM writes and reads 0x0000 pattern with 0 errors | □ | □ |
| FRAM-03 | FRAM write speed > 1 MHz SPI throughput | □ | □ |

### 6.6 Safety Supervisor Communication

Via service port:

```
> safety_status
Safety MCU: STM32G071RBT6
Firmware: v1.0.0
WDT State: RUNNING (feeds at 10ms)
INHIBIT Line: ASSERTED (active)
Last Heartbeat ACK: 8ms ago
SPI CRC: OK
```

| Check | Criterion | Pass | Fail |
|---|---|---|---|
| SAFE-01 | Safety MCU firmware version reported correctly | □ | □ |
| SAFE-02 | Safety MCU acknowledges LCU heartbeat within 15ms | □ | □ |
| SAFE-03 | INHIBIT line asserted on startup (correct default state) | □ | □ |
| SAFE-04 | Safety MCU SPI comms active — no SPI errors in 60-second observation | □ | □ |

---

## 7. LSO-100 Bring-Up (LPB-100 + LCU-100 Connected)

### 7.1 Initial Connection

1. Connect LPB-100 J3 (MiniFit 8-pin) to LSO-100 J1 (MiniFit 6-pin) using CAB-3.
2. Connect LCU-100 J2 (M12 5-pin) to LSO-100 J2 (M12 6-pin) with CAN/RS485 harness.
3. Connect LOAD-1 (representative LED lamp, 1.2W per aspect) to LSO-100 J3 (Approach A).
4. Verify INHIBIT line is asserted before applying power.

### 7.2 Power-Up and Inhibit Verification

Apply power. Verify all output channels are inhibited:

| Check | Criterion | Pass | Fail |
|---|---|---|---|
| LSO-PWR-01 | 12V present at LSO-100 TP_12V (12.0V ± 0.3V) | □ | □ |
| LSO-PWR-02 | All output channels measure 0V at J3–J7 pins (INHIBIT active) | □ | □ |
| LSO-PWR-03 | No LSO fault flags in CAN status message 0x020 | □ | □ |
| LSO-PWR-04 | INA219 devices respond on I2C (addresses 0x40–0x43) | □ | □ |

### 7.3 ALL_RED Phase Command

Via service port:

```
> phase_command ALL_RED
Phase: ALL_RED requested
Safety MCU: PERMISSIVE
INHIBIT: DE-ASSERTED
Output: Approach A RED energised
Output: Approach B RED energised (if connected)
```

Measure voltage at J3 Pin 1 (Approach A RED) with DMM-1:

| Check | Criterion | Measured (V) | Pass | Fail |
|---|---|---|---|---|
| OUT-01 | Approach A RED: voltage present, 11.0–13.5V | | □ | □ |
| OUT-02 | Approach A AMBER: 0V (not energised) | | □ | □ |
| OUT-03 | Approach A GREEN: 0V (not energised) | | □ | □ |
| OUT-04 | Lamp current (LOAD-1, 1.2W at 12V = 100mA): 90–110mA via INA219 | | □ | □ |
| OUT-05 | INA219 reading vs. current clamp CLAMP-1 within 10% | | □ | □ |

### 7.4 Phase Cycling Test (Approach A)

Command each phase in sequence and verify correct lamp energisation:

```
> phase_command AMBER --approach A --duration 5
> phase_command GREEN --approach A --duration 5
> phase_command ALL_RED
```

**Note:** GREEN command requires safety MCU permissive signal. Safety MCU will only permit GREEN after confirming no conflicting approaches are GREEN and minimum all-red clearance time has elapsed.

| Check | Criterion | Pass | Fail |
|---|---|---|---|
| PHASE-01 | AMBER energises within 200ms of command | □ | □ |
| PHASE-02 | GREEN energises within 200ms of command (with safety MCU permissive) | □ | □ |
| PHASE-03 | All-red clearance time ≥ 2 seconds before competing GREEN permitted | □ | □ |
| PHASE-04 | Phase transitions logged to FRAM event log | □ | □ |
| PHASE-05 | CAN status message 0x021 reports correct phase state | □ | □ |

### 7.5 Open-Circuit Fault Detection

Disconnect LOAD-1 from J3 while ALL_RED is active (lamp removed from live output):

| Check | Criterion | Pass | Fail |
|---|---|---|---|
| OC-01 | Open-circuit fault detected within 500ms | □ | □ |
| OC-02 | Fault event logged to FRAM with timestamp | □ | □ |
| OC-03 | CAN fault frame transmitted (ID 0x030, Fault_LSO) | □ | □ |
| OC-04 | System transitions to ALL_RED and holds on open-circuit fault | □ | □ |

### 7.6 Short-Circuit Protection Test

> **Warning:** Stand clear of J3 connector. Current will spike to BTS7030 limit (~21A) before overcurrent protection activates.

Connect LOAD-2 (1Ω 10W wirewound test load) across J3 Pin 1 and Pin 9 (GND) while channel is energised in ALL_RED.

| Check | Criterion | Pass | Fail |
|---|---|---|---|
| SC-01 | BTS7030 activates current limiting within 1µs (no sustained overcurrent) | □ | □ |
| SC-02 | Channel disabled (DIAG pin asserted) within 5ms | □ | □ |
| SC-03 | Fault event logged: SHORT_CIRCUIT Ch1 with timestamp | □ | □ |
| SC-04 | Other channels remain functional — no cascade fault | □ | □ |
| SC-05 | Channel recovers after fault cleared and reset command sent | □ | □ |

---

## 8. Integration Test — 2-Way Shuttle Mode

### 8.1 Setup

1. Connect approach head lamp loads to J3 (Approach A) and J4 (Approach B) on LSO-100.
2. Ensure CAN bus is connected with TERM-1 terminators at both ends.
3. Load 2-way shuttle phase plan via service port:

```
> plan_load SHUTTLE_2WAY
Plan loaded: SHUTTLE_2WAY
  Phase 1: Approach A GREEN, Approach B RED, Duration 30s (configurable)
  Phase 2: ALL_RED clearance, Duration 2s (minimum)
  Phase 3: Approach A RED, Approach B GREEN, Duration 30s (configurable)
  Phase 4: ALL_RED clearance, Duration 2s (minimum)
  Conflict matrix: A_GREEN | B_GREEN = FORBIDDEN
```

### 8.2 Continuous 10-Minute Run

Start the phase plan and observe for 10 minutes:

```
> plan_start
Plan started at 2026-06-18 14:45:00
```

| Check | Criterion | Pass | Fail |
|---|---|---|---|
| INT-01 | Correct phase sequencing throughout 10-minute run | □ | □ |
| INT-02 | Phase durations within ±200ms of plan specification | □ | □ |
| INT-03 | All-red clearance time ≥ 2.0 seconds in every cycle | □ | □ |
| INT-04 | No spontaneous fault events during normal operation | □ | □ |
| INT-05 | CAN heartbeat continuous — no bus-off events | □ | □ |
| INT-06 | FRAM event log shows correct phase transitions only | □ | □ |

### 8.3 Conflict Matrix Verification

Attempt to force both approaches GREEN simultaneously via debug command:

```
> debug_force_output APPROACH_A_GREEN APPROACH_B_GREEN
```

Expected response:
```
CONFLICT DETECTED: A_GREEN + B_GREEN violates conflict matrix
Safety MCU: INHIBIT ASSERTED
Request rejected — reverted to ALL_RED
Event logged: CONFLICT_ATTEMPT 2026-06-18 14:52:33
```

| Check | Criterion | Pass | Fail |
|---|---|---|---|
| CONF-01 | Conflict attempt rejected by safety MCU | □ | □ |
| CONF-02 | INHIBIT asserted within 30ms | □ | □ |
| CONF-03 | Conflict attempt logged to FRAM with timestamp | □ | □ |
| CONF-04 | System returns to ALL_RED after conflict detection | □ | □ |

### 8.4 Radio Link Loss Fallback

Simulate radio link loss by disabling the radio module (remove ANT-1 or command RF off):

```
> radio_disable
```

Monitor CAN bus for fallback behaviour:

| Check | Criterion | Pass | Fail |
|---|---|---|---|
| RADIO-01 | Radio link loss detected within 5 seconds | □ | □ |
| RADIO-02 | System falls back to ALL_RED within configured timeout (default 10s) | □ | □ |
| RADIO-03 | FALLBACK event logged to FRAM | □ | □ |
| RADIO-04 | System resumes normal operation when radio link restored | □ | □ |

---

## 9. Fault Injection Tests

### 9.1 Test Case List

All 20 fault injection test cases shall be executed in sequence. Record Pass/Fail and any observations.

| TC# | Test Case | Fault Injection Method | Expected Result | Pass | Fail | Observations |
|---|---|---|---|---|---|---|
| FI-01 | MCU Watchdog Expiry | `> debug_wdt_expire` (suspend WDT feed via debug command) | System resets, boots to ALL_RED, fault event WATCHDOG_EXPIRE logged | □ | □ | |
| FI-02 | Safety MCU SPI Comms Loss | Disconnect TP_SWD_SEL to interrupt SPI bus between U1 and U2 | INHIBIT asserted within 30ms, fault event SAFETY_MCU_COMMS_LOSS logged, ALL_RED maintained | □ | □ | |
| FI-03 | Battery Voltage Below 10.5V (Warning) | Reduce PSU-1 to 10.4V | Warning logged: BATT_LOW_WARNING, operation continues, CAN alert sent, no phase interruption | □ | □ | |
| FI-04 | Battery Voltage Below 9.5V (Shutdown) | Reduce PSU-1 to 9.4V | Safe shutdown sequence: ALL_RED held for 30s minimum, then controlled power-off, event BATT_CRITICAL logged | □ | □ | |
| FI-05 | Lamp Open Circuit Channel 1 | Remove lamp load from J3 Pin 1 while ALL_RED active | Fault logged: LAMP_OC_CH1 with timestamp, ALL_RED maintained, CAN fault frame transmitted within 500ms | □ | □ | |
| FI-06 | Lamp Short Circuit Channel 1 | Apply 1Ω load across J3 Pin 1/GND while channel energised | BTS7030 fault: channel disabled within 5ms, fault logged: LAMP_SC_CH1, other channels unaffected | □ | □ | |
| FI-07 | CAN Bus-Off (Disconnect Terminator) | Remove 120Ω terminator from CAN bus during active operation | CAN fault logged: BUS_OFF, automatic recovery attempted every 1 second, system holds ALL_RED during comms loss | □ | □ | |
| FI-08 | Door Switch Open (Tamper) | Open door switch input on J6 Pin 1 (simulate enclosure door open) | Tamper event logged: DOOR_OPEN with timestamp, telemetry alert sent via radio/CAN, operation continues | □ | □ | |
| FI-09 | Firmware CRC Failure Simulation | `> debug_corrupt_app_crc` (corrupt application CRC in flash) | Bootloader detects CRC failure, boots to safe fallback image, event FIRMWARE_CRC_FAIL logged | □ | □ | |
| FI-10 | Phase Timer Expired Without Completion | `> debug_block_phase_advance` then start phase plan | Phase advances to next phase on timeout, fault logged: PHASE_TIMEOUT, operation continues | □ | □ | |
| FI-11 | FRAM Write Failure Simulation | `> debug_fram_fault` (assert CS inactive, simulate SPI fault) | FRAM fault logged to internal RAM buffer, CAN alert: FRAM_FAULT, system continues with RAM-only logging | □ | □ | |
| FI-12 | INA219 Current Sensor I2C Failure | Remove U7 (INA219 @ 0x40) from I2C bus via jumper | I2C fault logged: CURRENT_SENSOR_FAIL_CH1-3, lamp current monitoring degraded warning issued, operation continues | □ | □ | |
| FI-13 | RTC Battery Failure | Remove BT1 coin cell, cycle power | RTC resumes from last known time with warning: RTC_NO_BACKUP, event logged, NTP/radio time sync attempted | □ | □ | |
| FI-14 | Overvoltage on Supply Rail | Increase PSU-1 to 16.0V | Overvoltage protection activates (TVS D1 on LPB), fault logged: SUPPLY_OV, system shuts down gracefully | □ | □ | |
| FI-15 | RS-485 Line Short Circuit | Short RS-485 A and B lines together | MAX3485 enters fault state, RS-485 fault logged, system falls back to CAN-only comms, alert issued | □ | □ | |
| FI-16 | INHIBIT Line Hardware Override | Assert INHIBIT line externally (drive low via test point TP_INHIBIT) | All output channels immediately de-energise, INHIBIT_ASSERTED event logged, system transitions to ALL_RED on de-assertion | □ | □ | |
| FI-17 | Multiple Simultaneous Channel Faults | Induce open-circuit on CH1, CH2, CH3 simultaneously | Each fault detected independently, all faults logged, system holds ALL_RED, CAN transmits multi-fault frame | □ | □ | |
| FI-18 | Power Supply Glitch (100ms dropout) | Momentarily interrupt PSU-1 for 100ms | System recovers from bulk capacitor, no phase interruption if dropout < 200ms, event logged: SUPPLY_DROPOUT | □ | □ | |
| FI-19 | LTE Modem Unresponsive | `> debug_modem_poweroff` (cut modem power via PMOS) | Modem fault logged: MODEM_UNREACHABLE, system falls back to local control, radio alert if LoRa available | □ | □ | |
| FI-20 | Safety MCU Watchdog Expiry (Independent) | `> debug_safety_wdt_expire` (suspend safety MCU WDT feed) | Safety MCU resets independently, INHIBIT asserted during reset, safety MCU re-synchronises with LCU within 2 seconds, event logged: SAFETY_MCU_RESET | □ | □ | |

### 9.2 Fault Injection Acceptance Criteria

All 20 test cases must achieve a PASS result before the prototype is approved for the 72-hour endurance run. Any FAIL result requires:
1. Root cause investigation
2. Hardware or firmware corrective action
3. Re-test of failed test case(s) and any related test cases
4. Updated test record sheet with corrective action documented

---

## 10. 72-Hour Endurance Run

### 10.1 Setup

1. All pre-power and bring-up checks must be complete with all items PASS.
2. All 20 fault injection test cases must be PASS.
3. Connect complete system: LPB-100, LCU-100, LSO-100, with representative lamp loads on J3 and J4.
4. Load 2-way shuttle phase plan with 60-second phase times.
5. Connect CAN-1 (PCAN-USB) to PC-1 running data logging software.
6. Connect service port to PC-1 running `lts_test001_auto.py --mode endurance_log`.

### 10.2 Endurance Run Configuration

| Parameter | Value |
|---|---|
| Phase Plan | SHUTTLE_2WAY |
| Phase A Duration | 60 seconds |
| Phase B Duration | 60 seconds |
| All-Red Clearance | 3 seconds |
| Ambient Temperature | 15–25°C (record start and end) |
| Run Duration | 72 hours minimum |
| CAN Logging | Continuous, all frames, to timestamped file |
| FRAM Event Log | Polled every 60 seconds, exported to CSV |

### 10.3 Pass Criteria

The 72-hour endurance run passes if ALL of the following criteria are met at the end of the run:

| Criterion | Requirement | Measured | Pass | Fail |
|---|---|---|---|---|
| END-01 | Zero unplanned phase transitions (excluding test fault injections) | 0 unplanned transitions | | □ | □ |
| END-02 | Zero unhandled exceptions or firmware crashes | 0 crashes | | □ | □ |
| END-03 | CAN bus error count < 10 total over 72 hours | < 10 bus errors | | □ | □ |
| END-04 | All-red clearance time never less than 2.0 seconds | Min recorded ≥ 2.0s | | □ | □ |
| END-05 | No lamp output voltage drift > ±5% from initial measurement | ±5% max | | □ | □ |
| END-06 | INA219 current readings within ±10% of clamp meter reference | ±10% max | | □ | □ |
| END-07 | MCU internal temperature (junction) < 85°C throughout | < 85°C | | □ | □ |
| END-08 | LPB-100 battery voltage within expected range throughout | 11.5–14.5V | | □ | □ |
| END-09 | FRAM event log contains no unexpected fault codes | 0 unexpected faults | | □ | □ |
| END-10 | System resumes correct operation after each scheduled all-red clearance | 100% correct | | □ | □ |

### 10.4 Monitoring During Endurance Run

The test engineer shall check the running system at the following intervals and sign the monitoring log:

| Interval | Checks |
|---|---|
| Every 4 hours | Visual inspection, PSU current reading, CAN bus error count check |
| Every 12 hours | DMM spot-check of 5V and 12V rails, lamp current verification |
| Every 24 hours | Export FRAM event log, review for unexpected faults |
| On completion | Full rail voltage measurements, thermal inspection, FRAM log export |

---

## 11. Sign-Off Table

This procedure is complete when all sections have been executed, all acceptance criteria met, and the sign-off table below completed by the authorised engineer.

| Section | Description | Result (PASS/FAIL) | Engineer | Date | Signature |
|---|---|---|---|---|---|
| 4 | Pre-Power Checks | | | | |
| 5 | LPB-100 Standalone Bring-Up | | | | |
| 6 | LCU-100 Bring-Up | | | | |
| 7 | LSO-100 Bring-Up | | | | |
| 8 | Integration Test — 2-Way Shuttle | | | | |
| 9 | Fault Injection Tests (all 20) | | | | |
| 10 | 72-Hour Endurance Run | | | | |

**Overall Result:**

| Overall PASS | Overall FAIL |
|---|---|
| □ | □ |

**Comments / Corrective Actions:**

_________________________________________________________________________________________________

_________________________________________________________________________________________________

**Authorising Engineer:**

Name: _____________________________ Signature: _____________________________ Date: _______________

**Witness:**

Name: _____________________________ Signature: _____________________________ Date: _______________

---

## Appendix A — Test Record Sheet

*Print one copy per board/prototype unit. Complete all fields.*

| Field | Value |
|---|---|
| Prototype Unit Serial No. | |
| LCU-100 PCB Serial No. | |
| LSO-100 PCB Serial No. | |
| LPB-100 PCB Serial No. | |
| Firmware Version (LCU App) | |
| Firmware Version (Safety MCU) | |
| Test Start Date/Time | |
| Test End Date/Time | |
| Test Engineer Name | |
| Test Location | |
| PSU-1 Asset No. / Cal Date | |
| DMM-1 Asset No. / Cal Date | |
| OSC-1 Asset No. / Cal Date | |
| CAN Analyser S/N | |

---

## Appendix B — Abbreviations

| Abbreviation | Meaning |
|---|---|
| ALL_RED | Traffic signal state: all approach aspects showing RED |
| BOM | Bill of Materials |
| CAN FD | Controller Area Network with Flexible Data-rate |
| CC/CV | Constant Current / Constant Voltage |
| DMM | Digital Multimeter |
| FRAM | Ferroelectric Random Access Memory |
| INHIBIT | Hardware signal that prevents all lamp outputs from energising |
| LCU | Lumina Controller Unit |
| LPB | Lumina Power and Battery board |
| LSO | Lumina Signal Output board |
| LPI | Lumina Portable Interface board |
| MPPT | Maximum Power Point Tracking |
| PSU | Power Supply Unit |
| RTC | Real-Time Clock |
| SWD | Serial Wire Debug |
| TVS | Transient Voltage Suppressor |
| WDT | Watchdog Timer |

---

*End of LTS-TEST-001 Rev A*
