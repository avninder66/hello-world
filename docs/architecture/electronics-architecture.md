# Lumina Temporary Traffic Signal Controller — Electronics Architecture

**Document Number:** LUM-ARCH-ELEC-001  
**Revision:** A  
**Date:** 2026-06-18  
**Status:** Released for Design

---

## Table of Contents

1. [System Overview and Design Philosophy](#1-system-overview-and-design-philosophy)
2. [Board Interconnection Topology](#2-board-interconnection-topology)
3. [LCU-100 — Lumina Control Unit](#3-lcu-100--lumina-control-unit)
4. [LSO-100 — Lumina Signal Output Board](#4-lso-100--lumina-signal-output-board)
5. [LPB-100 — Lumina Power and Battery Board](#5-lpb-100--lumina-power-and-battery-board)
6. [LPI-100 — Lumina Pedestrian Interface Board](#6-lpi-100--lumina-pedestrian-interface-board)
7. [System Power Architecture](#7-system-power-architecture)
8. [CAN FD Network Topology](#8-can-fd-network-topology)
9. [EMC Design Strategy](#9-emc-design-strategy)

---

## 1. System Overview and Design Philosophy

The Lumina temporary traffic signal controller is a portable, battery-backed system designed to manage single or multi-phase signalised junctions at roadworks sites. The system must operate unattended for up to 72 hours on internal battery power, withstand the harsh electromagnetic and environmental conditions of a roadway site, and meet the functional safety intent of IEC 61508 SIL 2 for output switching functions.

### 1.1 Design Objectives

- **Reliability:** All critical signalling functions must tolerate single-point hardware faults without creating an unsafe output state (all-red or all-off preferred over spurious green).
- **Serviceability:** Boards are modular and field-replaceable without special tooling. Each board is independently powered and communicates over a common CAN FD bus.
- **Connectivity:** Remote monitoring and programming via LTE cellular network; local configuration via USB-C service port.
- **Power autonomy:** Integrated lithium battery pack with MPPT solar charging and intelligent power management to maximise runtime.
- **EMC compliance:** The system must comply with EN 55032 Class B (emissions) and EN 55035 (immunity) for use on UK/EU public roads.

### 1.2 Functional Safety Philosophy

The architecture separates the functional path (main MCU executing phase logic) from the safety path (independent supervisor MCU monitoring outputs and inhibiting the lamp drivers on fault detection). This two-channel approach is the basis for the SIL 2 intent and follows the principles of IEC 61508 Part 2, Section 7.4.3 (hardware fault tolerance of 1 for SIL 2).

The safety supervisor on the LCU-100 (STM32G071RBT6) independently monitors:
- CAN FD heartbeat messages from all boards
- Lamp current feedback from LSO-100 via dedicated SPI channel
- Hardware watchdog expiry on the main MCU
- Conflict detection (simultaneous conflicting green outputs)

On detection of any of the above faults, the supervisor asserts a hardware INHIBIT line that disables all LSO-100 output drivers directly, independent of the main MCU.

### 1.3 Modular Architecture Summary

| Board   | Function                                  | Form Factor    |
|---------|-------------------------------------------|----------------|
| LCU-100 | Central controller, comms, phase logic    | 160 × 100 mm   |
| LSO-100 | Lamp driver outputs (6 channels)          | 140 × 100 mm   |
| LPB-100 | Battery management, MPPT charger, 12V bus | 120 × 80 mm    |
| LPI-100 | Pedestrian push-button interface          | 80 × 60 mm     |

---

## 2. Board Interconnection Topology

### 2.1 Physical Bus Architecture

The four boards communicate over a shared CAN FD bus running at 1 Mbps (arbitration) / 4 Mbps (data phase). The LCU-100 is the bus master and sole keeper of the phase state machine. All other boards are slaves responding to LCU-100 commands and sending periodic telemetry frames.

A secondary RS-485 bus provides a low-speed (115200 baud) diagnostic channel between the LCU-100 and field-deployed LPI-100 units. This bus is used for push-button state, pedestrian demand signals, and audible/tactile confirmation commands. RS-485 was selected for the field connection because it is inherently half-duplex, uses a differential pair with 120 Ω termination, and is robust to the longer cable runs (up to 30 m) between a controller box and a remote pedestrian post.

```
                        ┌─────────────────────────────────────────┐
                        │              LCU-100 (Bus Master)        │
                        │  STM32H743  │  STM32G071  │  LTE/GNSS   │
                        └──────┬──────┴──────┬───────┴─────┬───────┘
                               │ CAN FD      │ CAN FD      │ RS-485
                    ┌──────────┼─────────────┼──────────┐  │
                    │          │             │          │  │
                ┌───▼───┐  ┌──▼────┐  ┌─────▼──┐  ┌───▼───▼──┐
                │LSO-100│  │LSO-100│  │LPB-100 │  │ LPI-100  │
                │(Node 1│  │(Node 2│  │(Node 3)│  │(Node 4)  │
                └───────┘  └───────┘  └────────┘  └──────────┘
```

CAN FD bus runs on a dedicated 2-wire cable within the controller chassis. Stubs from the backbone to each board are kept below 5 m (preferred < 300 mm within the chassis) to maintain signal integrity at 4 Mbps data phase. 120 Ω termination resistors are fitted at both ends of the backbone — one at the LCU-100 and one at the furthest LSO-100.

### 2.2 Power Distribution Topology

The LPB-100 generates the 12 V system bus and distributes it via a dedicated power cable harness to all other boards. Each board has its own local DC-DC regulation. This star distribution from LPB-100 ensures that a short-circuit fault on one board does not collapse the power rail for other boards, because each branch is protected by a per-board polyfuse on the LPB-100 output header.

```
  Battery Pack ──→ LPB-100 (BMS + MPPT) ──→ 12V Bus
                              │
              ┌───────────────┼───────────────┐
              │               │               │
           LCU-100         LSO-100         LPI-100
          (3.3V, 5V)     (12V direct,   (3.3V, 5V)
                          5V logic)
```

---

## 3. LCU-100 — Lumina Control Unit

### 3.1 Board Overview

The LCU-100 is the primary intelligence of the Lumina system. It executes the traffic phase state machine, manages all inter-board communications, provides remote telemetry and configuration via LTE, logs events to non-volatile memory, and hosts the independent safety supervisor MCU.

### 3.2 Main Microcontroller — STM32H743ZIT6

**Manufacturer:** STMicroelectronics  
**Package:** LQFP-144  
**Part Number:** STM32H743ZIT6

The STM32H743ZIT6 was selected as the main MCU due to its combination of processing performance, peripheral breadth, and availability within the automotive/industrial supply chain.

**Key specifications used in this design:**
- Core: 400 MHz Cortex-M7 with double-precision FPU and 16 KB L1-I/D caches
- Flash: 2 MB dual-bank (enables live firmware update without halting execution)
- RAM: 1 MB SRAM (512 KB AXI + 128 KB TCM + 128 KB DTCM + 256 KB D2/D3 SRAM)
- FDCAN: 3 × FDCAN controllers (ISO 11898-2, up to 8 Mbps); FDCAN1 to CAN FD bus, FDCAN2 to supervisor, FDCAN3 reserved
- SPI: 6 × SPI/I2S; used for FRAM, GNSS, display header, and supervisor communications
- I2C: 4 × I2C; used for RTC, fuel gauge, board sensors
- UART: 4 × USART + 4 × UART; used for LTE modem (UART4), debug console (USART1), RS-485 (USART2)
- USB: USB OTG HS with embedded PHY
- ADC: 3 × 16-bit SAR ADC, used for battery voltage monitoring and auxiliary sense
- GPIO: 114 GPIOs sufficient for all control, status LED, and discrete fault detection functions
- Supply: 3.3 V core; 1.8 V IO bank for GNSS SPI
- Temperature range: -40°C to +85°C (industrial grade suffix I)
- Package thermal resistance: θJA = 28.4°C/W (LQFP-144 on 4-layer board with exposed pad)

**External oscillator:** 25 MHz HSE crystal (Abracon ABLS-25.000MHZ-B4-T) driving the PLL. The LQFP-144 HSE pins are on a short < 5 mm stub. Load capacitors 18 pF (MLCC 0402 C0G NP0).

**Boot mode:** BOOT0 pin connected via 10 kΩ pull-down to GND with a factory-accessible 2-pin header to force DFU mode. Normal operation: BOOT0 = 0 (boot from Flash bank 1).

**JTAG/SWD:** 10-pin Cortex debug header (SWD + SWO) on J5. Connected to STM32H743 PA13/PA14 (SWDIO/SWDCLK) and PB3 (SWO).

### 3.3 Safety Supervisor MCU — STM32G071RBT6

**Manufacturer:** STMicroelectronics  
**Package:** LQFP-64  
**Part Number:** STM32G071RBT6

The STM32G071RBT6 provides the independent hardware safety path. It is clocked from its own 32 MHz internal RC oscillator (trimmed to ±1%) and has no shared clock source with the main MCU, ensuring single fault coverage for oscillator failures.

**Key specifications used in this design:**
- Core: 64 MHz Cortex-M0+
- Flash: 128 KB (sufficient for safety monitor firmware ≤ 50 KB)
- RAM: 36 KB
- Independent watchdog (IWDG): separate from main MCU IWDG; must be petted by the safety monitor task, which only runs if the main MCU is sending valid heartbeats
- FDCAN: 1 × FDCAN; dedicated FDCAN2 link to main MCU (private supervisory channel)
- SPI: Used to read lamp current ADC results from INA219 channels
- GPIO: INHIBIT_N output (active-low, open-drain, 47 Ω series) drives hardware enable chain on LSO-100
- Supply: 3.3 V from dedicated LDO rail (AP2114HA-3.3TRG1 separate instance) so that a LDO fault on the main 3.3 V does not disable the supervisor

**Supervisory functions implemented:**
1. Heartbeat monitoring: Main MCU transmits a CAN FD frame on FDCAN2 every 50 ms; supervisor resets a 200 ms timeout counter on valid receipt. Timeout → INHIBIT asserted.
2. Conflict detection: Supervisor maintains a model of active green outputs. If conflicting greens are commanded simultaneously, INHIBIT is asserted within one supervisory task cycle (5 ms).
3. Current fault: If any INA219 reports > 150% rated current for > 100 ms on a channel commanded OFF, INHIBIT is asserted.
4. Brownout: Supervisor monitors its own VDD via internal ADC. If VDD < 3.0 V for > 10 ms, INHIBIT is asserted.

### 3.4 FRAM — MB85RS4MT

**Manufacturer:** Fujitsu / Cypress (now Infineon)  
**Package:** SOP-8  
**Part Number:** MB85RS4MT

**Specification:** 4 Mbit (512 KB) SPI FRAM, 3.3 V supply, 40 MHz SPI clock, 10^13 write/erase cycles, 10 year data retention at +85°C.

The MB85RS4MT is used for event log storage and configuration persistence. The 10^13 endurance specification means that writing a 512-byte log record every second continuously would take over 600,000 years to exhaust the device — eliminating the wear-levelling overhead required when using NOR or NAND Flash.

Connected to SPI1 on STM32H743 (pins PA5/PA6/PA7/PA4 for SCK/MISO/MOSI/CS). SPI clock configured at 20 MHz for margin against trace capacitance. CS line held high via 10 kΩ pull-up during MCU reset.

The FRAM holds:
- Last 4000 event log records (128 bytes each = 512 KB total)
- Current phase schedule configuration (up to 16 plans × 256 bytes = 4 KB)
- Fault history and counters
- Factory calibration constants

### 3.5 Real-Time Clock — MCP7940N

**Manufacturer:** Microchip Technology  
**Package:** SOIC-8  
**Part Number:** MCP7940N-I/SN

The MCP7940N provides timekeeping accurate to ±2 ppm when disciplined from the LTE network (NTP) and ±20 ppm free-running from the 32.768 kHz crystal.

**Crystal:** Abracon ABS07-32.768kHz-7-T, ±20 ppm, 7 pF load capacitance, MSOP-2 package. Load capacitor calculation: CL = (C1 × C2)/(C1 + C2) + Cstray. For CL = 7 pF target, C1 = C2 = 12 pF (C0G 0402) with Cstray estimated at 2 pF.

**Backup power:** ML2032 lithium coin cell (3 V, 65 mAh) on VBAT pin via 1N4148WS Schottky diode (forward drop 350 mV). This maintains timekeeping for ≥ 2 years with the main power removed.

**Interface:** I2C on I2C1 (PB6/PB7), address 0x6F. Interrupt output (MFP pin) connected to PB0 on STM32H743 for alarm wake from low-power mode.

### 3.6 CAN FD Transceivers — TCAN1042VDRQ1

**Manufacturer:** Texas Instruments  
**Package:** SOIC-8  
**Part Number:** TCAN1042VDRQ1

Two TCAN1042VDRQ1 transceivers are fitted on the LCU-100: one for the main board CAN FD bus (driven by STM32H743 FDCAN1) and one for the private supervisor channel (driven by STM32G071 FDCAN1).

**Key specifications:**
- Conforms to ISO 11898-2:2016 and SAE J2284-4 (CAN FD up to 5 Mbps)
- Supply: 5 V (±10%) from isolated CAN supply (RECOM R05P05D/P)
- Logic I/O: 3.3 V/5 V compatible (VIO pin tied to 3.3 V)
- Bus fault protection: ±58 V DC bus fault, ±12 kV HBM ESD on CAN pins
- Thermal shutdown, short-circuit protection, dominant state timeout (TXD stuck-low protection: 750 µs typ)
- Common-mode range: −7 V to +12 V

**EMC filtering:** Each transceiver's CAN_H and CAN_L lines are filtered with a WURTH 744231220 common-mode choke (2 × 22 µH, 200 mA) and 100 pF C0G capacitors to GND on each line. The CM choke is placed within 5 mm of the connector. A 120 Ω split termination (2 × 60 Ω, 0402, 1%) with a 4.7 nF bypass capacitor to ground is fitted at the LCU-100 end of the bus.

### 3.7 RS-485 Transceiver — MAX3485EESA+

**Manufacturer:** Maxim Integrated (now Analog Devices)  
**Package:** SOIC-8  
**Part Number:** MAX3485EESA+

The MAX3485EESA+ provides the half-duplex RS-485 interface to field-deployed LPI-100 pedestrian units. Selected for:
- 3.3 V single-supply operation (no 5 V required on RS-485 supply rail)
- ±15 kV ESD protection (IEC 61000-4-2 Level 4 on A/B pins)
- Slew-rate limiting: max 10 Mbps, but configured for 115200 baud; the controlled edge rates reduce EMI without requiring additional filtering
- Extended temperature range: −40°C to +85°C

Connected to USART2 on STM32H743 (PA2/PA3 for TX/RX). Direction pin (DE/RE) driven by PA1 GPIO in open-drain push-pull mode, toggled under DMA control for minimum turnaround latency. Half-duplex framing uses the UART idle line detection interrupt to trigger DE deassertion.

**Line protection:** PESD2CAN TVS diode array (dual-line, 6 V clamp) on A and B pins. 120 Ω termination switchable via relay K1 (Panasonic TQ2SA-5V) to allow end-of-line or mid-line configurations.

### 3.8 USB-C Port — TUSB1310A + PRTR5V0U2X

**USB Bridge Controller — TUSB1310A**  
**Manufacturer:** Texas Instruments  
**Package:** VQFN-64  
**Part Number:** TUSB1310A

The TUSB1310A USB 3.1 Gen 1 hub and USB-C controller manages the service USB-C port (J3 on LCU-100). It enumerates as a composite USB device presenting:
- USB CDC-ACM serial port (debug console / CLI)
- USB Mass Storage (access to FRAM-backed log exports)
- USB DFU device (firmware update)

The TUSB1310A handles CC logic, VBUS detection, and USB PD negotiation (up to 5 V / 0.9 A for charging). It interfaces to the STM32H743 via USB OTG HS (PA11/PA12).

**ESD Protection — PRTR5V0U2X**  
**Manufacturer:** Nexperia  
**Package:** SOT-363  
**Part Number:** PRTR5V0U2X

The PRTR5V0U2X dual ESD protection array protects D+ and D− lines. It provides < 0.5 pF capacitance per line (critical for USB 3.x signal integrity) and ±15 kV contact discharge per IEC 61000-4-2. Placed within 2 mm of the USB-C connector pins.

### 3.9 LTE Modem — Quectel EC21-A

**Manufacturer:** Quectel Wireless Solutions  
**Interface:** M.2 B-key socket, form factor 3042  
**Part Number:** EC21AUTPA-512-STD (EC21-A module, 512 MB Flash, standard firmware)

The Quectel EC21-A provides Cat 1 LTE connectivity for remote monitoring, configuration push, and NTP time synchronisation. It is mounted on an M.2 B-key socket (J6, TE Connectivity 2199230-4) which allows field replacement without soldering.

**Key specifications:**
- LTE Cat 1: DL 10 Mbps / UL 5 Mbps
- Bands (EC21-A): LTE B2/B4/B5/B12/B13/B17 (North America); specify EC21-E for EU/UK (B1/B3/B5/B7/B8/B20)
- UART interface: 115200 baud AT command set on USART4 (PA0/PA1 on STM32H743)
- SIM: Nano-SIM card holder on LCU-100 (Amphenol C707 10M008 052 2) or embedded SIM option
- Antenna: SMA bulkhead connector (J7) feeding 50 Ω microstrip to M.2 ANT pin
- Supply: 3.8 V from dedicated linear regulator LP38693MP-3.8 (500 mA, LDO, transient immune)
- Control: PWRKEY and RESET_N GPIOs on PC2/PC3 of STM32H743

**RF keepout:** A 15 mm keepout zone around the M.2 socket prohibits copper on all layers except GND plane. The 50 Ω feed trace (125 µm wide on FR4, outer layer) has a continuous GND plane reference within 200 µm.

### 3.10 GNSS Module — u-blox SAM-M10Q

**Manufacturer:** u-blox AG  
**Package:** LCC-16, 9.6 × 9.6 mm  
**Part Number:** SAM-M10Q-00B-00

The SAM-M10Q provides GPS/GLONASS/Galileo/BeiDou GNSS for:
- Site location stamping in event logs and remote telemetry
- Precise time of day (PPS output) for sub-microsecond event timestamping
- Geo-fence monitoring (automatic all-red if controller moves outside permitted site boundary)

**Key specifications:**
- 18 dBHz sensitivity (tracking)
- Time to first fix: 2 s (hot start), 26 s (cold start)
- PPS accuracy: 30 ns RMS (under open sky)
- Supply: 1.71–1.89 V (VCORE) + 1.71–3.6 V (VIO) — supplied from 1.8 V LDO ADP1714AUJZ-1.8-R7
- Interface: SPI (primary, on SPI2) and I2C (secondary); PPS on PC8 (TIM3_CH3 input capture for sub-µs timestamping)
- Antenna: Active patch antenna (TAOGLAS FXP73) via SMA connector on PCB edge; LNA supply 3.3 V via L-C filter on antenna supply pin

**Patch antenna placement:** SAM-M10Q is located in the top-right corner of the LCU-100 with a 20 × 20 mm ground-free keepout area beneath the antenna footprint on all layers below the antenna PCB.

### 3.11 Power Supervision — MCP809T-315I/TT

**Manufacturer:** Microchip Technology  
**Package:** SOT-23-3  
**Part Number:** MCP809T-315I/TT

The MCP809T-315I/TT is a dedicated 3.15 V threshold reset supervisor for the 3.3 V rail, providing an active-low RESET output (open-drain, 100 ms assertion delay) that is ORed with the STM32H743 NRST pin. The internal brown-out detector (BOR) threshold on the STM32H743 is set to Level 3 (2.7 V) as a secondary guard.

This dual-supervision architecture ensures that transient droops on the 3.3 V rail (caused by inrush during LTE modem transmit bursts) that might cause erratic MCU behaviour trigger a clean reset rather than corrupt the state machine.

**Supervisor for 5 V rail:** A separate MCP809T-460I/TT (4.60 V threshold) monitors the isolated 5 V CAN supply. On 5 V fault, a GPIO interrupt is generated to STM32H743 PD0 and the CAN transceivers are hardware-disabled.

### 3.12 5 V Regulator — TPS7A4700RGWT

**Manufacturer:** Texas Instruments  
**Package:** VQFN-20 (RGW, 4 × 4 mm)  
**Part Number:** TPS7A4700RGWT

The TPS7A4700RGWT is a 36 V input, 1 A LDO selected specifically for its ultra-low noise output (4 µVRMS, 10 Hz–100 kHz). This performance is required because the LTE modem and GNSS module share a common 5 V-derived supply path, and noise on the supply modulates the RF VCO, degrading receiver sensitivity.

**Configuration:**
- Output voltage: 5.0 V set by external resistor divider R1 (VSET1 network); R1 = 10 kΩ, R2 = 10 kΩ (NV1 pin = 1.0 V reference)
- Input: 12 V bus (after reverse-protection Schottky)
- Power dissipation at 1 A load: (12 − 5) × 1 = 7 W; requires 2 cm² copper pour + 2 × 2 thermal via array to inner GND plane, or external heatsink tab solder
- Enable: active-high EN pin controlled by STM32G071 PB6 (supervisor can cut 5 V rail on fault)
- Bypass capacitors: 10 µF + 100 nF on NR/SS pin per datasheet recommendation for noise optimisation

### 3.13 3.3 V Regulator — AP2114HA-3.3TRG1

**Manufacturer:** Diodes Incorporated  
**Package:** SOT-223-3  
**Part Number:** AP2114HA-3.3TRG1

The AP2114HA-3.3TRG1 provides the main 3.3 V rail from 5 V. The two-stage regulation (12 V → 5 V → 3.3 V) minimises heat dissipation at each stage.

**Specifications:** 800 mA continuous, PSRR 70 dB at 1 kHz, dropout 300 mV at 800 mA, −40°C to +125°C.

Input capacitor: 10 µF tantalum (KEMET T491B106M010AT) for bulk + 100 nF C0G 0402 for HF decoupling.  
Output capacitor: 10 µF ceramic X5R 0805 (MURATA GRM21BR61E106KA73) + 100 nF C0G.

A second instance of the AP2114HA-3.3TRG1 (U25) provides the independent 3.3 V supply for the STM32G071 supervisor, fed from the same 5 V bus but with a separate enable path controlled by the MCP809T-460I/TT.

### 3.14 Isolated 5 V DC-DC — RECOM R05P05D/P

**Manufacturer:** RECOM Power  
**Package:** SIP-7  
**Part Number:** R05P05D/P

The R05P05D/P is a 1 W isolated DC-DC converter (5 V input, ±5 V output, 100 mA per rail) providing galvanic isolation between the CAN FD transceiver bus side and the board logic ground. This isolation is mandatory to break ground loops in multi-board configurations where each board may be powered from different distribution points on the 12 V bus.

**Configuration:** Only the +5 V output is used (−5 V output is left open-circuit with a 100 nF filter cap to the −Vout pin). The isolated +5 V feeds the VCC pin of both TCAN1042VDRQ1 transceivers. The isolated GND is the reference for the bus-side termination capacitors and common-mode choke shield.

Output filter: 10 µH shielded inductor (Bourns SRR1260-100Y) + 47 µF / 10 V polymer capacitor (Panasonic EEFCX0J470XR) suppresses the 200 kHz switching frequency.

---

## 4. LSO-100 — Lumina Signal Output Board

### 4.1 Board Overview

The LSO-100 provides 6 independently controllable high-current output channels for driving LED or incandescent traffic signal lamp heads. Each channel is rated at 5 A continuous (12 V / 24 V operation) with peak current capability of 21 A for cold-filament incandescent lamp inrush.

The board receives switching commands from the LCU-100 over CAN FD, drives outputs via high-side PROFET switches, and reports per-channel current consumption back to the LCU-100 and safety supervisor.

### 4.2 High-Side Switches — BTS7030-2EPA

**Manufacturer:** Infineon Technologies  
**Package:** PG-DSO-14 (exposed pad)  
**Part Number:** BTS7030-2EPA

Three BTS7030-2EPA devices are used on the LSO-100, each providing two independent high-side switch channels, totalling 6 channels. The BTS7030-2EPA is a dual-channel automotive-grade PROFET (PROtected FET) designed specifically for resistive and capacitive lamp load applications.

**Key specifications per channel:**
- On-state resistance: 30 mΩ typical (at 25°C, Vbat = 13.5 V)
- Continuous current: 12.5 A per channel (thermal limitation, device rated to 21 A peak)
- Supply voltage: 5 V to 40 V (operated from 12 V or 24 V battery bus)
- Overcurrent threshold: 48 A (hardware latch-off with automatic retry)
- Overtemperature threshold: 175°C junction (automatic shutdown)
- Open-load detection: proportional current sense (IS output) detects open circuit when channel is OFF
- Reverse polarity protection: integrated body diode sustains reverse battery connection
- SPI diagnostics: 4-wire SPI interface reports per-channel fault status, IS ratio, and device temperature
- dV/dt slew control: configurable via SLEW_RATE pin to limit lamp in-rush current ramp rate
- EMC: integrated active clamp on inductive load flyback (energy rating 60 mJ per clamp event)

**SPI interface:** BTS7030-2EPA SPI uses SPI_CLK, SPI_MOSI, SPI_MISO, CSN pins. All three devices share the SPI bus (SPI2 on the LSO-100 local MCU, or bitbanged GPIO if LSO-100 uses a simpler controller). CS lines are individual per device (GPIO PB4, PB5, PB9).

**Thermal design:** Each BTS7030-2EPA package has a bottom-side exposed thermal pad. A 4 × 4 array of 0.3 mm diameter thermal vias under the exposed pad connects to an internal 2 oz copper plane. A 15 × 15 mm aluminium-filled thermal interface material (Bergquist GP3000S30, 3.0 W/m·K) is used between the PCB and the controller chassis wall, which acts as a heatsink (thermal resistance chassis-to-ambient 5°C/W).

Maximum allowable power dissipation per device at full 5 A per channel continuous:  
P = 2 × I² × RDS(on) = 2 × (5)² × 0.030 = 1.5 W  
Junction temperature rise: 1.5 W × θJC (3.3°C/W) = 5°C — well within limits.

### 4.3 Current Sensing — INA219BIDCNT

**Manufacturer:** Texas Instruments  
**Package:** SOT-23-5 (DCN)  
**Part Number:** INA219BIDCNT

One INA219BIDCNT per channel pair provides high-side current monitoring. The INA219B is a 12-bit current/power monitor with an I2C interface and internal 12-bit ADC.

**Configuration:**
- Shunt resistor: 10 mΩ, 1%, Vishay WSBS2816 2512, 3 W power rating
- Full-scale range: ±320 mV / 10 mΩ = ±32 A (configured for ±16 A with GAIN = /2 for 12-bit resolution)
- LSB current resolution: 0.5 mA (sufficient for LED open-load detection at 50 mA minimum load)
- I2C addresses: set by A0/A1 pins; three devices at 0x40, 0x41, 0x42
- Alert pin: connected to LSO-100 MCU interrupt GPIO for over-current alert without polling

**Current sensing topology:** The INA219 shunt is inserted in the high-side feed to the BTS7030-2EPA supply pin (not in the lamp output path) to capture total per-pair consumption including BTS7030 quiescent supply.

### 4.4 Input Protection — SMBJ18A TVS Diodes

**Manufacturer:** Vishay / ON Semiconductor  
**Package:** SMB (DO-214AA)  
**Part Number:** SMBJ18A

One SMBJ18A unidirectional TVS diode is fitted per output channel from the output pin to GND. The SMBJ18A has a 18 V standoff voltage, 20 V reverse standoff, and 29 V clamping voltage at 13.1 A. This protects against inductive load flyback transients that exceed the BTS7030-2EPA's internal active clamp for very high-energy events (>60 mJ).

On the supply input (12 V bus connection), a SMBJ24A (24 V standoff, 40 V clamp) protects against load-dump transients per ISO 7637-2 pulse 5b (up to 40 V open-circuit).

### 4.5 Hardware Inhibit Chain

The LSO-100 incorporates a hardware inhibit chain that disables all BTS7030-2EPA IN pins simultaneously when the INHIBIT_N signal from the LCU-100 safety supervisor is asserted. The chain logic is:

```
INHIBIT_N (from LCU-100) → HCPL-314J opto-isolator → 74LVC1G08 AND gate input A
                                                                          |
Software_Enable (from LSO MCU) ─────────────────────────────────── AND gate input B
                                                                          |
                                                                   AND output → BTS7030 IN pins (all 6)
```

**Optical Isolator — HCPL-314J**  
**Manufacturer:** Broadcom (formerly Avago)  
**Package:** DIP-8  
**Part Number:** HCPL-314J

The HCPL-314J is a 2.5 A gate drive optocoupler with an integrated IGBT/MOSFET gate driver output stage. Although this application does not require the gate drive current, the HCPL-314J was selected for its isolation voltage (2500 Vrms), high CMR (> 10 kV/µs), and rail-to-rail output compatible with the 3.3 V logic on the AND gate. The high dV/dt immunity is important given that the inhibit line runs alongside the 12 V switched lines in the harness.

**AND Gate — 74LVC1G08**  
**Manufacturer:** Texas Instruments / Nexperia  
**Package:** SOT-23-5  
**Part Number:** SN74LVC1G08DBVR (TI) or 74LVC1G08GW (Nexperia)

The 74LVC1G08 implements the two-input AND function between the optocoupler output (representing INHIBIT_N deasserted = HIGH) and the software enable signal from the LSO MCU. This ensures that even a firmware bug enabling outputs when INHIBIT_N is asserted cannot produce a lamp output, because the hardware AND gate is outside the firmware execution path.

Gate propagation delay: 3.9 ns typical — the inhibit function takes effect within one CAN FD bit period of assertion.

### 4.6 Input Fuse and OR-ing Diode

**Blade Fuse:** 30 A ATO-blade fuse (Littelfuse 0297030.ZXNV) in a PCB-mounted fuseholder (Keystone 3568). Rated for 32 V DC operation. Provides board-level overcurrent protection against wiring short circuits.

**Ideal Diode — SI7288DP**  
**Manufacturer:** Vishay Siliconix  
**Package:** PowerPAK SO-8 Dual  
**Part Number:** SI7288DP

The SI7288DP dual N-channel MOSFET is configured as an ideal diode for reverse polarity protection and ORing. Each FET has RDS(on) = 8.5 mΩ at VGS = 10 V, giving < 250 mW dissipation at 5 A. The gate is driven by the LTC4412HMS8 PowerPath controller on the LPB-100 when used in the ORing application. For reverse polarity on the LSO-100 input, a self-driven gate clamp (1N4148WS + 10 kΩ + 10 V zener BZX84C10) clamps the gate to allow reverse body diode conduction only as a fail-safe.

---

## 5. LPB-100 — Lumina Power and Battery Board

### 5.1 Board Overview

The LPB-100 manages the lithium battery pack, solar MPPT charging, power sequencing, and distribution of the 12 V system bus. It is the power source and distribution hub for the entire Lumina system.

### 5.2 Battery Management IC — BQ76952

**Manufacturer:** Texas Instruments  
**Package:** WQFN-76  
**Part Number:** BQ76952PFBR

The BQ76952 is a fully integrated 3-series to 16-series lithium battery monitor, balancer, and protection controller. In the Lumina LPB-100, it monitors a 4S1P lithium iron phosphate (LiFePO4) pack (nominal 12.8 V, 50 Ah, built from CATL prismatic cells).

**Key functions used:**
- Cell voltage measurement: 16-channel, 16-bit differential ADC, ±1 mV accuracy per cell
- Pack current measurement: Coulomb counter via external 0.5 mΩ shunt resistor (Isabellenhütte BVR-Z-R0005-1.0, 4-terminal, 75 A rated)
- Passive cell balancing: 20 mA balance current per cell via internal FETs (adequate for top-balancing during charge)
- Protection FETs: Controls external CHG and DSG N-channel MOSFETs for charge and discharge path isolation
  - CHG FET: PSMN1R4-40YLD (1.4 mΩ, 40 V, D²PAK) — see §5.6
  - DSG FET: PSMN1R4-40YLD (same device, separate instance)
- Fault handling: Overvoltage (3.65 V/cell), undervoltage (2.5 V/cell), overcurrent discharge (200 A, 15 ms delay), short circuit (> 500 A, < 1 ms delay)
- Communications: I2C at 400 kHz (address 0x08) to LPB-100 local MCU (STM32G031K8T6)
- Temperature inputs: 3 × thermistor inputs (NTC, 10 kΩ @ 25°C) monitoring cell pack, PCB, and charger FET temperatures

### 5.3 MPPT Charger IC — BQ25798

**Manufacturer:** Texas Instruments  
**Package:** VQFN-30 (4 × 4 mm)  
**Part Number:** BQ25798RQMR

The BQ25798 is a 3-cell to 8-cell bidirectional battery charger with integrated MPPT algorithm, supporting input voltages from 3.0 V to 32 V and battery voltages from 3.5 V to 22 V.

**Configuration for LPB-100:**
- Input: 12 V / 24 V solar panel input (via LTC4412 ideal diode ORing)
- Battery: 4S LiFePO4, 14.6 V maximum charge voltage
- Maximum charge current: 20 A (set by 5.1 kΩ ILIM resistor on PROG pin)
- MPPT algorithm: voltage-ratio method (configured to 80% Voc setpoint); updated every 30 s
- OTG (reverse boost) mode: can back-feed from battery to input when configured as an emergency power supply
- I2C interface (address 0x6B) to LPB-100 MCU for telemetry and configuration
- Protections: BATOVP, BATOCP, BUSOCP, BUSOVP all integrated; external TVS provides load-dump protection

**Solar Panel Input Range:** System designed for 100 W monocrystalline panel (Voc 22.3 V, Vmp 18.1 V, Isc 5.9 A). The BQ25798 input range fully covers the expected operating range. Two 30 A Schottky diodes (MBRB20200CTG, TO-263) provide protection against panel reverse current and ORing between dual panels.

### 5.4 Ideal Diode ORing — LTC4412HMS8

**Manufacturer:** Analog Devices (Linear Technology)  
**Package:** MSOP-8  
**Part Number:** LTC4412HMS8#PBF

Two LTC4412HMS8 PowerPath controllers provide ideal diode OR-ing between:
1. Battery output and solar charger output (U7): prioritises charger when available, falls back to battery
2. 12 V bus output and external 12 V backup input (U8): allows connection of external generator power source

The LTC4412 drives a P-channel MOSFET gate to maintain near-zero forward drop (< 20 mV) when the selected source is active, while the non-selected path is blocked by reverse gate bias. The low forward drop (vs. a Schottky diode) significantly reduces heat dissipation and improves efficiency at high currents.

**P-channel MOSFET for ORing:** PMOS FDT86244L (−30 V, 6.3 A, 58 mΩ, SOT-23-6), with gate threshold −1.5 V (LTC4412 can fully enhance even with limited overdrive at low temperatures).

### 5.5 Reverse Polarity Protection — PSMN1R4-40YLD

**Manufacturer:** Nexperia  
**Package:** LFPAK56 (Power-SO8)  
**Part Number:** PSMN1R4-40YLD

The PSMN1R4-40YLD is a 40 V N-channel MOSFET with RDS(on) = 1.4 mΩ at VGS = 10 V and 75 A continuous current capability. Two instances are used on LPB-100:

1. **Reverse polarity protection** on the battery connector: The MOSFET is inserted source-to-drain in the positive supply rail (source on battery +, drain to bus). The gate is clamped to the battery − terminal via a 12 V gate clamp (Zener BZX84C12 + 10 kΩ pull-up). With correct polarity, VGS = +12 V, FET conducts. With reverse polarity, VGS = −12 V (would damage gate), but the 12 V Zener clamps the gate to safe levels and the body diode blocks reverse current.

2. **High-current discharge switch** (controlled by BQ76952 DSG output): Provides the battery discharge enable function as required by the BMS protection algorithm.

At rated current (20 A continuous), power dissipation = I² × RDS(on) = 400 × 0.0014 = 0.56 W — negligible.

### 5.6 Load Dump Protection — SMAJ40CA + 100 V Gate Clamp

**TVS Diode — SMAJ40CA**  
**Manufacturer:** STMicroelectronics / Vishay  
**Package:** SMA (DO-214AC)  
**Part Number:** SMAJ40CA

The SMAJ40CA bidirectional TVS (40 V standoff, 68 V clamping at peak pulse current) provides protection on the 12 V bus input from load-dump transients up to 600 W (10/1000 µs pulse per ISO 7637-2). One fitted per LPB-100 input terminal.

The 100 V gate-rated clamp referenced in the design is a JFET-configuration gate protection network using BZT52C10 Zener diodes in series (2 × 10 V = 20 V clamp) on the PSMN1R4-40YLD gate, rated to 100 V transient. This prevents a fast common-mode transient from punching through the gate oxide during a load-dump event.

### 5.7 Fuel Gauge — BQ28Z610

**Manufacturer:** Texas Instruments  
**Package:** TSSOP-24  
**Part Number:** BQ28Z610DRZR

The BQ28Z610 implements the Impedance Track fuel gauging algorithm, which estimates state of charge based on open-circuit voltage, cell impedance, and coulomb counting. It communicates over SMBus (I2C-compatible, 100 kHz, address 0x55) to the LPB-100 MCU.

**Key outputs reported over SMBus:**
- RemainingCapacity (mAh) — used to calculate estimated runtime
- StateOfCharge (%) — displayed on LCU-100 status interface
- Temperature — battery pack temperature from internal thermistor input
- CycleCount — maintenance indicator for battery replacement planning
- ChargingCurrent / ChargingVoltage — optimal charge setpoints fed to BQ25798

The BQ28Z610 shares the same current shunt as the BQ76952 (Isabellenhütte BVR-Z-R0005-1.0) via a dedicated sense amplifier input, ensuring consistent current measurement.

### 5.8 12 V Bus Buck Regulator — LMR54406XYFPR

**Manufacturer:** Texas Instruments  
**Package:** WSON-8 (2.5 × 3 mm)  
**Part Number:** LMR54406XYFPR

The LMR54406XYFPR is a 42 V input, 6 A synchronous buck converter operating at a fixed 2.1 MHz switching frequency. It is used on the LPB-100 to generate a regulated 12.0 V output from the raw battery bus (which may range from 10.0 V to 15.0 V depending on LiFePO4 state of charge).

**Configuration:**
- Output voltage: 12.0 V set by R_TOP = 100 kΩ, R_BOT = 9.76 kΩ (feedback ratio per datasheet)
- Switching frequency: 2.1 MHz (above AM band, below FM, minimising antenna interference)
- Output ripple: < 20 mVpp with 47 µH inductor (Bourns SRR1260-470Y) and 2 × 100 µF / 16 V polymer output capacitors (Panasonic EEFCX1C101P)
- EN pin: controlled by BQ76952 FET drive signal; if BMS shuts down battery, regulator is disabled simultaneously
- Power Good: open-drain PG output to LPB-100 MCU PC0 interrupt for rail fault detection

At maximum load (12 V bus supplying 3 × LSO-100 @ 5 A each = 15 A total + LCU-100 @ 2 A + LPI-100 × 2 @ 0.5 A each = 18 A total):  
LMR54406 is rated 6 A — this device is used for the logic supply subsystem only. The main 12 V battery bus is directly derived from the battery pack output through the BMS protection FETs; the LMR54406 is specifically used for a clean 12.0 V reference for the LCU-100 and LPI-100 boards where the regulated voltage is required regardless of battery state.

### 5.9 Board Temperature Sensor — PCT2075DP

**Manufacturer:** NXP Semiconductors  
**Package:** SOIC-8  
**Part Number:** PCT2075DP,118

The PCT2075DP is an I2C digital temperature sensor with ±1°C accuracy from −25°C to +100°C, 0.125°C resolution, and programmable alert thresholds. Two instances are fitted on LPB-100:
- U20 (address 0x48): monitors PCB temperature near charger FETs
- U21 (address 0x49): monitors PCB temperature near battery connector

Alert output (OS pin) triggers an interrupt on the LPB-100 MCU to reduce charge current if PCB temperature exceeds 60°C, and halt charging above 70°C.

---

## 6. LPI-100 — Lumina Pedestrian Interface Board

### 6.1 Board Overview

The LPI-100 is mounted inside a pedestrian push-button post and provides:
- Push-button demand input with debounce and ESD protection
- Wait lamp (amber LED) drive
- Audible signal unit (high-pitched beep at green)
- Tactile rotating cone (solenoid actuated) for visually impaired users
- CAN FD (chassis) or RS-485 (field cable) communication back to LCU-100

### 6.2 Push Button Input — RC Debounce + SN74LVC1G17

**Debounce filter:**  
Series resistor: 47 Ω (0402, 1%, 1 kΩ alternative for higher impedance lines)  
Shunt capacitor: 100 nF (MLCC 0402, X5R, 16 V)  
RC time constant: 47 × 100n = 4.7 µs — sufficient to reject contact bounce (typically 1–20 ms) when combined with firmware debounce.

**Schmitt Trigger Buffer — SN74LVC1G17**  
**Manufacturer:** Texas Instruments  
**Package:** SOT-23-5  
**Part Number:** SN74LVC1G17DBVR

The SN74LVC1G17 provides a hysteresis buffer (1.2 V typ at 3.3 V supply) on the debounced push-button signal, preventing oscillation around the switching threshold due to contact resistance. Output is a clean 3.3 V digital signal connected to the LPI-100 MCU GPIO PA0 (EXTI interrupt).

**ESD protection on button input cable:** PRTR5V0U2X (dual-channel, same part as USB protection on LCU-100) on the cable entry pins provides ±15 kV ESD protection on a line that may run up to 2 m of unshielded cable inside the pedestrian post.

### 6.3 Button Illumination — 2N7002 MOSFET

**Manufacturer:** ON Semiconductor / Nexperia  
**Package:** SOT-23-3  
**Part Number:** 2N7002LT1G (ON Semi) or 2N7002BU (Nexperia)

The 2N7002 N-channel MOSFET (60 V, 300 mA, VGS(th) = 1.5–2.5 V, RDS(on) = 7.5 Ω) drives the wait lamp LED (12 V, 50 mA white LED in the push-button assembly). Gate driven by 3.3 V GPIO via 100 Ω series resistor. Drain connected to LED cathode; LED anode connected to 12 V through 180 Ω current-limiting resistor (sets I_LED = (12 − 3.2) / 180 = 48.9 mA).

The 2N7002 gate threshold requires > 1.5 V to conduct. GPIO logic-high at 3.3 V provides > 1 V margin.

### 6.4 Audible Output — PAM8008 + TDA8551

**Class-D Amplifier — PAM8008**  
**Manufacturer:** Diodes Incorporated  
**Package:** SOIC-16  
**Part Number:** PAM8008TR

The PAM8008 is an 8 W filterless Class-D audio amplifier in mono bridge-tied load (BTL) configuration, operating from a 5 V supply. It drives the internal sounder speaker (8 Ω, 87 dB SPL, 1 m) fitted inside the pedestrian post housing.

Output power at 5 V into 8 Ω: 2.5 W (THD = 1%), which gives measured SPL of approximately 91 dB at 1 m — meeting the DfT requirement for audible crossing signals (≥ 75 dB at 1 m).

**Tone generation:** A tone signal at 880 Hz (A5) is generated by PWM timer TIM14_CH1 on the LPI-100 MCU (STM32G031K6T6), filtered to a sinewave by a 2nd-order Sallen-Key RC filter (R = 1.8 kΩ, C = 100 nF, fc = 884 Hz) before feeding the PAM8008 input.

**Line-Level Driver — TDA8551**  
Note: The TDA8551 is configured as a volume control and input buffer stage between the MCU PWM output and the PAM8008 input, providing software-adjustable volume (I2C control) and proper impedance matching. The TDA8551 is an 8-bit I2C-controlled volume control IC (1 dB step, −79 dB to 0 dB range, SOIC-8 package) connected to I2C1 on the LPI-100 MCU.

### 6.5 Tactile Cone Drive — DRV8833

**Manufacturer:** Texas Instruments  
**Package:** WQFN-16 (3 × 3 mm)  
**Part Number:** DRV8833PWPR

The DRV8833 is a dual H-bridge motor driver (10 V, 1.5 A per channel) used to drive the rotating tactile cone solenoid as a bidirectional actuator. The solenoid is a 12 V, 1.2 W rotary solenoid (Geeplus SMR-170-12) that rotates a cone-shaped nub on the underside of the push-button unit to indicate the green phase to visually impaired pedestrians.

**Drive configuration:**
- AIN1/AIN2 driven by LPI-100 MCU PB4/PB5 (TIM3_CH1/CH2 PWM outputs)
- Cone extend: AIN1 = high, AIN2 = low (forward current)
- Cone retract: AIN1 = low, AIN2 = high (reverse current)
- nSLEEP pin driven by PA8: pulled low to put DRV8833 into sleep mode (< 1 µA) when not active
- nFAULT: open-drain output to PA9 interrupt; triggers if FET overcurrent or thermal shutdown occurs
- Peak coil current limited to 1.2 W / 12 V = 100 mA by coil impedance — well within DRV8833 1.5 A rating

### 6.6 CAN FD Transceiver — TCAN1042VDRQ1

Same device as LCU-100 (§3.6). On the LPI-100, the transceiver is operated from 3.3 V logic (VIO = 3.3 V) and 5 V bus supply. The LPI-100 supports both CAN FD (for chassis-mounted units) and RS-485 (for field cable connected units) via J3 (8-way connector, Molex Micro-Fit 3.0) that contains both bus pairs. Jumper JP1 selects CAN FD or RS-485 mode.

---

## 7. System Power Architecture

### 7.1 12 V Bus Generation and Distribution

The battery pack (4S LiFePO4, 10.0–14.6 V range) directly forms the 12 V system bus after passing through:
1. Battery connector reverse-polarity protection MOSFET (PSMN1R4-40YLD)
2. BQ76952 controlled discharge FET (PSMN1R4-40YLD)
3. LTC4412 ideal diode ORing with solar charger output

The resulting 12 V bus (labelled VBAT_BUS in schematics, range 10.0–14.6 V) is distributed to all boards over a dedicated 2-wire harness (2.5 mm² wire, rated 25 A, red/black colour coded).

### 7.2 Per-Board Local Regulation

| Board   | 12V → 5V Regulator        | 5V → 3.3V Regulator         | Additional Rails            |
|---------|---------------------------|-----------------------------|-----------------------------|
| LCU-100 | TPS7A4700RGWT (1 A LDO)   | AP2114HA-3.3TRG1 (0.8 A LDO) | 3.8 V for modem, 1.8 V for GNSS |
| LSO-100 | 78M05 (0.5 A LDO, logic only) | AP2114HA-3.3TRG1           | 12V direct to BTS7030       |
| LPB-100 | LMR54406 (6 A buck)       | AP2114HA-3.3TRG1            | VBAT_BUS direct to charger  |
| LPI-100 | AMS1117-5.0 (1 A LDO)     | AP2114HA-3.3TRG1            | 5 V to PAM8008              |

### 7.3 Power Budget

At full operational load (2 × LSO-100, all 6 channels active at 3 A average, all boards operating):

| Board   | 12V Current | Power     |
|---------|-------------|-----------|
| LCU-100 | 0.5 A       | 6 W       |
| LSO-100 (×2) | 6 A × 2 | 144 W  |
| LPB-100 | 0.3 A       | 3.6 W     |
| LPI-100 (×2) | 0.3 A × 2 | 7.2 W |
| **Total** | **13.4 A** | **160.8 W** |

Battery capacity at 50 Ah: Runtime = 50 Ah / 13.4 A = 3.73 hours at full load. At typical traffic signal duty cycle (30% lamp-on time), runtime extends to approximately 11 hours. With 100 W solar charging, net load falls to approximately 6 A during daytime, extending daytime runtime indefinitely under adequate solar conditions.

---

## 8. CAN FD Network Topology

### 8.1 Bus Configuration

| Parameter            | Value                                     |
|----------------------|-------------------------------------------|
| Nominal bit rate     | 1 Mbps (arbitration phase)                |
| Data phase bit rate  | 4 Mbps                                    |
| Bit time quanta      | 80 ns (12.5 MHz / 1 Mbps)                 |
| Bus length (max)     | 5 m within chassis                        |
| Node count           | Up to 8 (LCU-100 + 7 slaves)             |
| Termination          | 120 Ω split (2 × 60 Ω + 4.7 nF to GND) at each end |
| Transceiver          | TCAN1042VDRQ1 (all nodes)                 |
| Frame format         | CAN FD base frame (11-bit ID)             |
| Protocol layer       | CANopen FD (CiA 1301 draft)               |

### 8.2 Node Addressing

| Node ID | Board       | Function                                    |
|---------|-------------|---------------------------------------------|
| 0x01    | LCU-100     | Bus master, NMT manager                    |
| 0x10    | LSO-100 #1  | Signal head outputs (approach 1)           |
| 0x11    | LSO-100 #2  | Signal head outputs (approach 2)           |
| 0x20    | LPB-100     | Power management, battery telemetry        |
| 0x30    | LPI-100 #1  | Pedestrian interface (approach 1)          |
| 0x31    | LPI-100 #2  | Pedestrian interface (approach 2)          |

### 8.3 Critical Timing Requirements

**Output switching latency:** From LCU-100 FDCAN1 TX to LSO-100 PROFET enable pin assertion: maximum 2 ms (one CAN FD frame at 1 Mbps + bus arbitration worst-case + LSO-100 interrupt latency + BTS7030 enable propagation).

**Heartbeat period:** Each node transmits a heartbeat frame every 50 ms. LCU-100 safety supervisor considers a node failed if no heartbeat received within 200 ms (4 × heartbeat period).

### 8.4 Stub Length and Signal Integrity

At 4 Mbps data phase, the maximum allowable stub length (unterminated branch) is calculated as:

Stub length = (propagation velocity × bit time) / 10  
= (0.66c × 0.25 µs) / 10 = 4.95 m maximum

Within the controller chassis (PCB-to-PCB cables ≤ 300 mm), all stubs are well within this limit. For field extensions to remote LSO-100 units (up to 5 m), the data phase rate is reduced to 2 Mbps to maintain signal integrity margin.

---

## 9. EMC Design Strategy

### 9.1 Conducted Emissions — Input Filtering

Each board's 12 V supply input incorporates a π-filter:
- C1: 100 µF / 25 V electrolytic (Panasonic ECA-1EHG101) — bulk storage and low-frequency filtering
- L1: 10 µH / 5 A common-mode choke (TDK ACM7060-101-2PL-TL) — suppresses CM current on supply wires
- C2: 10 µF / 16 V ceramic (Murata GRM32ER71C106KA01) + 100 nF C0G — high-frequency decoupling

This topology targets > 40 dB attenuation at frequencies above 150 kHz, the lower CISPR limit for conducted emissions testing.

### 9.2 CAN FD EMC Filtering

CAN FD differential lines are particularly susceptible to radiated emissions at the data-phase frequency (4 Mbps → harmonics at 8, 12, 16 MHz in the HF broadcast bands). Each TCAN1042VDRQ1:

- Receives a ferrite bead (Murata BLM31PG600SN1L, 600 Ω @ 100 MHz, 200 mA) in series on each CAN_H and CAN_L trace before the PCB edge connector
- Is preceded by a WURTH 744231220 common-mode choke (22 µH, 200 mA) on the cable side
- Has 100 pF / 50 V C0G capacitors from CAN_H and CAN_L to isolated GND (placed at connector, within 5 mm of pins)

The isolated GND (CAN bus reference) is connected to board GND via a 1 MΩ resistor + 4.7 nF capacitor in parallel, providing a high-impedance DC path to prevent floating bus-side ground while maintaining AC isolation.

### 9.3 RF Isolation for LTE/GNSS

The LTE modem (EC21-A) and GNSS module (SAM-M10Q) are located in the top-right corner of the LCU-100, separated from digital switching circuitry by a 5 mm RF keepout zone. The STM32H743 clock tree's 400 MHz system clock and its harmonics must be kept > 60 dB below the LTE receiver noise floor.

Measures implemented:
1. Shielding can (Laird Technologies BMI-S-212-F) over the M.2 modem socket: soldered to GND pads around the perimeter, providing > 30 dB shielding at 700–2700 MHz
2. Separate 3.8 V LDO supply for modem (LP38693MP-3.8) with 100 µH / 100 mA ferrite bead (Murata BLM18KG121TN1D) on supply line
3. All fast digital signals (SPI, USART, high-frequency GPIO) routed to the opposite (bottom-left) corner of the board, with at least 10 mm separation from RF traces
4. 50 Ω coplanar waveguide (CPW) with continuous GND reference within 200 µm used for LTE antenna trace; any bend uses mitered corners (45°)

### 9.4 ESD Protection Summary

| Location                       | Protection Device          | Standard Tested To         |
|-------------------------------|----------------------------|----------------------------|
| USB-C D+/D−                   | PRTR5V0U2X                 | IEC 61000-4-2 Level 4 (±8 kV) |
| RS-485 A/B field lines         | PESD2CAN                   | IEC 61000-4-2 Level 4 (±8 kV) |
| CAN_H / CAN_L (TCAN1042)      | Internal (±58 V DC fault)  | ISO 7637-2 Pulse 1/2/3a/3b |
| Push-button cable inputs (LPI)| PRTR5V0U2X                 | IEC 61000-4-2 Level 4 (±8 kV) |
| 12 V bus input (each board)   | SMBJ24A TVS                | ISO 7637-2 Pulse 5b (40 V) |
| LTE SMA antenna port          | RCLAMP0524T                | IEC 61000-4-2 Level 4 (±8 kV) |

---

*Document controlled by Lumina Engineering. For revision history see LUM-ARCH-ELEC-001 revision register.*
