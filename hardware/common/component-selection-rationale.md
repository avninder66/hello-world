# Lumina Component Selection Rationale

**Document Number:** LUM-HW-COMP-001  
**Revision:** A  
**Date:** 2026-06-18  
**Author:** Lumina Electronics Engineering  
**Status:** Released for Design Review

---

## Purpose

This document records the engineering rationale behind key component choices in the Lumina temporary traffic signal controller. It is intended to serve as a reference for design reviews, future revision decisions, and supply chain alternatives qualification. Each section documents the selected part, the alternatives evaluated, and the technical and commercial reasoning for the selection.

---

## Table of Contents

1. [Main MCU: STM32H743ZIT6](#1-main-mcu-stm32h743zit6)
2. [Safety Supervisor MCU: STM32G071RBT6](#2-safety-supervisor-mcu-stm32g071rbt6)
3. [Lamp Drivers: PROFET BTS7030-2EPA vs Discrete MOSFET](#3-lamp-drivers-profet-bts7030-2epa-vs-discrete-mosfet)
4. [Event Log Storage: FRAM MB85RS4MT vs Flash and EEPROM](#4-event-log-storage-fram-mb85rs4mt-vs-flash-and-eeprom)
5. [Inter-Board Communication: CAN FD vs Ethernet vs RS-485](#5-inter-board-communication-can-fd-vs-ethernet-vs-rs-485)
6. [Battery Management: BQ76952 vs Discrete Analogue Front-End](#6-battery-management-bq76952-vs-discrete-analogue-front-end)
7. [Operating Temperature Range: -30°C to +70°C Justification](#7-operating-temperature-range--30c-to-70c-justification)

---

## 1. Main MCU: STM32H743ZIT6

### 1.1 Decision

**Selected:** STM32H743ZIT6 (STMicroelectronics, LQFP-144, 400 MHz Cortex-M7)

### 1.2 Alternatives Evaluated

**Option A: NXP i.MX RT1170 (MIMXRT1170DVMAA)**

The i.MX RT1170 is a dual-core (Cortex-M7 + Cortex-M4) crossover MCU running at 1 GHz (M7) + 400 MHz (M4). It offers higher raw performance and advanced features including hardware JPEG codec and 2D graphics acceleration.

Rejection reasons:
1. **Excessive capability:** The Lumina controller's phase state machine is not computationally intensive (< 10% CPU utilisation estimated). The dual-core architecture adds complexity without functional benefit for a deterministic event-driven state machine.
2. **Peripheral count for this application:** The i.MX RT1170 has fewer CAN FD controllers (2 FLEXCAN vs 3 FDCAN on H743) in the base device. Adding a CAN FD controller would require an external SPI-to-CAN device (MCP2517FD), adding cost, board area, and a firmware layer.
3. **Ecosystem and toolchain:** The Lumina firmware team has existing STM32 HAL/CubeMX experience. The i.MX RT1170 uses NXP MCUXpresso and has a different HAL layer; adopting it for this programme would incur training and porting time.
4. **Package availability:** LQFP-144 (STM32H743) is stocked at Mouser, Farnell, and DigiKey in the thousands. The BGA-289 package required for i.MX RT1170 needs specialist soldering capability not available at preferred EMS partner.

**Option B: Renesas RH850/F1KM-S4**

The RH850/F1KM is an automotive-grade microcontroller (ASIL-D, ISO 26262) used in body control modules, with built-in lockstep Cortex-R52 cores.

Rejection reasons:
1. **ASIL-D overhead:** The RH850/F1KM targets ASIL-D automotive applications. Its development toolchain (GHS Multi, Renesas CS+) requires software qualification artefacts (MISRA-C, DO-178C analogues) that would more than double firmware development cost and schedule for a SIL 2 target.
2. **CAN FD peripheral:** RH850/F1KM uses Renesas RSCAN peripheral, which has a different frame FIFO architecture than STM32 FDCAN. Driver porting is a significant effort.
3. **GNSS and USB OTG integration:** The RH850 does not include USB OTG HS — an external USB bridge (CH340 or similar) would be required for the service port, adding BoM cost and increasing failure modes.
4. **Supply chain:** Renesas RH850 devices require a Renesas-authorised design registration for pricing; lead times in 2024–2025 have exceeded 52 weeks. STM32H743 is consistently available through standard distribution.

### 1.3 Selection Justification

The STM32H743ZIT6 meets all computational and peripheral requirements with significant headroom:
- 3 × FDCAN controllers: LCU board bus + supervisor private channel + reserved for expansion
- 6 × SPI: FRAM, GNSS, display (future), flash (future), supervisor, reserved
- USB OTG HS: eliminates need for external bridge
- 2 MB dual-bank flash: supports FOTA (firmware-over-the-air) via LTE without halting operation during update
- Industrial temperature grade (-40°C to +85°C): exceeds the −30°C to +70°C system requirement with 10°C margin
- LQFP-144 package: hand-solderable and inspectable without X-ray for rework during prototyping
- STM32CubeH7 HAL and FreeRTOS port are available open-source, reducing development risk
- Long-term availability: STM32H7 series was introduced in 2017 and is in STMicroelectronics stated 10-year longevity commitment (product last order date ≥ 2032)

---

## 2. Safety Supervisor MCU: STM32G071RBT6

### 2.1 Safety Architecture Rationale

IEC 61508 Part 2 defines two approaches to achieving SIL 2 with hardware:
- **Single-channel with diagnostic coverage:** One MCU implementing the function with sufficient self-test to achieve SFF ≥ 90%, HFT = 0
- **Dual-channel (1oo2D):** Two independent channels each executing the function; comparison logic detects discrepancies

The Lumina architecture adopts a **functional separation** approach: the main MCU (STM32H743) executes the phase logic (which is not itself a safety function — incorrect phase output is a hazard, but the main MCU cannot directly create the hazard without the output enable path). The safety supervisor (STM32G071) implements the monitoring and inhibit functions, which are the safety function under IEC 61508 evaluation.

This means the supervisor must:
1. Operate on independent power supply (separate LDO, separate crystal oscillator)
2. Have no shared firmware or data memory with the main MCU
3. Communicate with the main MCU via a monitored channel (CAN FD heartbeat) rather than shared memory
4. Have a direct hardware path to disable outputs without main MCU involvement

### 2.2 STM32G071 vs STM32G031 vs External Safety MCU

**Why not STM32G031K8T6 (smaller, cheaper)?**  
The STM32G031 has 64 KB Flash and 8 KB RAM. The supervisor firmware includes CAN FD driver, INA219 SPI driver, phase conflict detection logic, IWDG management, and I2C telemetry reporting — estimated at 48 KB code. The G031 would be marginal; during development the code size will grow. STM32G071 (128 KB Flash, 36 KB RAM) provides comfortable margin at a cost difference of approximately £0.40 per unit — not a significant saving given the £600+ total unit cost.

**Why not a dedicated safety IC (e.g., TI TMS570LS0232)?**  
The TMS570LS0232 is a dual-core lockstep ARM Cortex-R4 MCU with IEC 61508 SIL 3 certification support. While it would provide a stronger safety argument, it is significantly over-specified for monitoring 6 binary output states and 6 current channels. The TMS570 requires the SafeTI Development Kit (additional licensing cost) and has no integrated CAN FD peripheral (uses CAN 2.0B only via DCAN, not CAN FD). The STM32G071 approach achieves the SIL 2 intent at lower cost and without needing a separate development environment.

**Independence of STM32G071 clock from STM32H743:**  
STM32G071 uses its internal RC oscillator (16 MHz trimmed, ±1%) as clock source. The H743 uses an external 25 MHz crystal. This ensures that an external clock failure (damaged crystal, broken oscillator trace) affects only one MCU, satisfying the independence requirement for the dual-channel approach.

---

## 3. Lamp Drivers: PROFET BTS7030-2EPA vs Discrete MOSFET

### 3.1 Selected Device

**Selected:** Infineon BTS7030-2EPA (Dual-channel PROFET, PG-DSO-14)

### 3.2 Discrete MOSFET Alternative

The simplest lamp driver implementation uses a discrete N-channel MOSFET (e.g., IPD90N04S4-04 or similar) in a high-side configuration with a gate driver IC (e.g., MC33883 or IR2110). This approach is used in many traffic control designs.

**Discrete MOSFET architecture for reference:**
- N-channel FET (low-side): simpler gate drive but introduces ground shift on lamp return path, complicating current sensing
- P-channel FET (high-side): simple gate drive, but P-channel FETs have 2–3× higher RDS(on) than equivalent N-channel, increasing power dissipation at 5 A
- N-channel FET (high-side) with bootstrap gate driver: lowest RDS(on) but requires gate drive supply above battery rail, adding complexity

**Comparison table: BTS7030-2EPA vs Discrete high-side MOSFET + gate driver**

| Feature                          | BTS7030-2EPA                                | Discrete: BSP452 P-ch + buffer |
|----------------------------------|---------------------------------------------|-------------------------------|
| RDS(on) at 25°C                 | 30 mΩ (N-channel internal FET)             | 180 mΩ (P-channel at VGS = −10V) |
| Power dissipation at 5 A         | 0.75 W per channel                          | 4.5 W per channel              |
| Overcurrent protection           | Integrated (48 A latch-off)                 | External sense resistor + comparator |
| Open-load detection              | Integrated (IS current mirror, off-state)  | Separate circuit required      |
| Overtemperature protection       | Integrated (175°C, auto-retry)             | Separate NTC + comparator      |
| Fault diagnostics                | SPI readback (fault code, IS ratio, Tj)    | Individual GPIO flags per fault|
| Component count (6 channels)     | 3 × BTS7030-2EPA (PROFET)                  | 6 FETs + 2 gate driver ICs + 6 Rs + 6 comparators = 20+ parts |
| PCB area (6 channels)           | ≈ 30 × 15 mm (3 PROFET packages)          | ≈ 60 × 30 mm (discrete solution)|
| Unit cost (6 channels, 2025)    | 3 × £2.80 = £8.40                           | ≈ £6.50 (discrete parts)      |
| Inrush current management        | Integrated slew rate control                | Requires RC on gate or soft-start circuit |
| ESD protection (output)         | ±2 kV HBM integrated on output pins       | Requires external TVS          |

### 3.3 Decision Rationale

The BTS7030-2EPA is marginally more expensive in unit terms but delivers compelling advantages:

1. **Integrated diagnostics via SPI:** The IS (current sense) output of the BTS7030-2EPA provides a proportional current sense signal (IS ratio = 1:3440 typ) that allows the safety supervisor to verify each channel's current state without additional current-sense hardware. A discrete solution would require 6 × INA219 (one per channel) rather than 3 × INA219 (one per pair) — because the BTS7030 IS output already provides per-channel indication.

2. **Active clamp for inductive loads:** Traffic signal lamp cables have significant inductance (measured 50–200 µH for a 10 m cable to a lamp head). Switching this inductance with a discrete FET requires an external freewheeling diode (DO-214AB package) and TVS diode for each channel. The BTS7030-2EPA integrates an active clamp that dissipates inductive flyback energy internally, rated for 60 mJ per event — sufficient for the inductances in this application.

3. **Automotive qualification:** Lumina controller units are deployed on public roads. The BTS7030-2EPA is qualified to AEC-Q101 (automotive component qualification) and tested to ISO 7637-2 for load-dump and transient immunity. Automotive-qualified components undergo accelerated life test (1000 hours HTOL at 150°C) and HTSS/LTSS qualification that general industrial FETs do not.

4. **Fault safety mode:** On SPI communication loss or supervisor assertion of INHIBIT_N, the BTS7030-2EPA latches off all outputs. This is a safe state (all lamps off → all-red condition via redundant red lamp on each head). Discrete gate-drive solutions require careful analysis of what happens to the gate when the MCU is reset — a floating gate can lead to unpredictable FET state.

---

## 4. Event Log Storage: FRAM MB85RS4MT vs Flash and EEPROM

### 4.1 Selected Device

**Selected:** Fujitsu MB85RS4MT (4 Mbit SPI FRAM, 512 KB)

### 4.2 NOR Flash Alternative (e.g., Winbond W25Q32JVSSIQ)

NOR Flash (e.g., W25Q32JV, 4 MB, SPI, £0.45 at volume) offers the largest capacity-to-cost ratio but has fundamental limitations for event logging:

**Endurance:** W25Q32JV is rated 100,000 write/erase cycles per 4 KB sector. Writing a 128-byte log record every second requires erasing a 4 KB sector approximately every 32 seconds (once the sector is full). At this rate, the flash endurance is exhausted in 100,000 × 32 s = 3.2 million seconds ≈ 37 days. This is unacceptable for a unit expected to operate for 5+ years.

**Mitigation by wear levelling:** A wear-levelling algorithm (similar to what NAND Flash FTLs implement) can extend Flash life by distributing writes across all sectors. A 4 MB Flash with 1000 sectors (4 KB each) at 100,000 cycles per sector could sustain 100,000 × 1000 / (32 s/sector) = 3.2 × 10^9 s ≈ 100 years in theory. However:
- Wear-levelling requires maintaining a flash translation table in RAM (minimum 4 KB for 1024-sector device)
- On power loss during a flash write, the translation table can become corrupted — requiring complex journalling
- Flash write latency: 4 KB page program takes 0.7 ms; sector erase takes 60–400 ms — this blocks the SPI bus during erase, potentially missing events
- Code complexity and testing burden is significant

**Conclusion on NOR Flash:** The wear-levelling overhead, power-loss data integrity risk, and erase latency make NOR Flash unsuitable for the primary event log.

### 4.3 I2C EEPROM Alternative (e.g., Microchip 25AA512)

I2C/SPI EEPROM (e.g., Microchip 25AA512, 512 Kbit, 64 KB, £0.65) solves the wear levelling complexity because EEPROMs perform byte-level writes without block erase. However:

**Endurance:** The 25AA512 is rated 1,000,000 write cycles per byte location. At 1 write/second to the same byte (worst case circular buffer pointer): 1,000,000 / (365 × 24 × 3600) = 31.7 days to failure. Even distributing writes across 64 KB: 1,000,000 × 65,536 bytes / (128 bytes/record × 1 record/second) = 512 × 10^6 seconds ≈ 16 years. Marginally acceptable, but with no guard band.

**Write latency:** 25AA512 page write (128 bytes max per page write cycle) takes 5 ms. During this 5 ms, the EEPROM BUSY condition requires either polling (blocking SPI bus) or interrupt-driven write with queuing logic.

**Capacity:** 64 KB stores only 512 × 128-byte records — approximately 8.5 minutes of 1-record/second logging. Insufficient for the 72-hour autonomous operation requirement (72 × 3600 = 259,200 seconds → 259,200 records × 128 bytes = 33 MB needed at 1 Hz logging rate; reduced to < 500 KB at 1 record/minute for non-event-driven logging).

### 4.4 FRAM Justification

The MB85RS4MT resolves all the above issues:

| Parameter                   | MB85RS4MT (FRAM)     | W25Q32JV (NOR Flash) | 25AA512 (EEPROM) |
|-----------------------------|----------------------|----------------------|------------------|
| Capacity                    | 512 KB               | 4 MB                 | 64 KB            |
| Write endurance             | 10^13 cycles/byte   | 10^5 cycles/sector   | 10^6 cycles/byte |
| Write speed (byte/page)     | 40 MHz SPI, instant  | 0.7 ms / 4 KB        | 5 ms / 128 B     |
| Read speed                  | 40 MHz SPI           | 104 MHz SPI          | 20 MHz SPI       |
| Wear levelling required     | No                   | Yes                  | No               |
| Power loss safe             | Yes (byte-atomic)    | Risk during erase    | Yes              |
| Unit cost (2025, volume)    | £3.20                | £0.45                | £0.65            |

**Endurance at 1 record/minute logging over 5 years:**  
10^13 cycles / (5 × 365 × 24 × 60 writes to same location) = 10^13 / 2.6 × 10^6 = 3.8 × 10^6 years  
Even at 1 Hz maximum logging: 10^13 / (5 × 365 × 86400) = 6.3 × 10^7 years.

The cost premium of the MB85RS4MT (£3.20 vs £0.45 for NOR Flash) is justified by:
1. Elimination of wear-levelling firmware module (estimated 40 person-hours to develop and test)
2. Elimination of power-loss journalling firmware (estimated 20 person-hours)
3. No erase latency — event records are written in a single SPI transaction
4. Simpler firmware reliability argument for functional safety evidence

---

## 5. Inter-Board Communication: CAN FD vs Ethernet vs RS-485

### 5.1 Selected Protocol

**Selected:** CAN FD (ISO 11898-2, 1 Mbps / 4 Mbps) for board-to-board within chassis.  
**Selected:** RS-485 (115200 baud, half-duplex) for field cable to remote LPI-100 units.

### 5.2 Ethernet Alternative (100BASE-T or 10BASE-T1L)

Ethernet offers very high bandwidth (100 Mbps) and a mature ecosystem (TCP/IP, MQTT, standard networking tools). An Ethernet-based inter-board network could use a small managed switch (e.g., Microchip KSZ8895MQXCA 5-port) and standard UDP frames.

Rejection reasons:
1. **Non-determinism:** Standard Ethernet with TCP/IP is non-deterministic. Under load, a packet may be delayed for milliseconds due to collision avoidance, backoff, or OS scheduling. For safety-critical output switching (the inhibit mechanism must respond within 200 ms), a non-deterministic bus is unacceptable without a Time-Sensitive Networking (TSN) implementation — which would require 802.1AS (gPTP) and 802.1Qbv (scheduled traffic), adding significant firmware and hardware complexity.
2. **Switch failure:** An Ethernet switch is a single point of failure for the entire inter-board network. A CAN bus with no active switch component has no equivalent single point of failure.
3. **Physical layer:** 100BASE-TX requires two pairs of shielded twisted pair or careful PCB trace impedance matching. 10BASE-T1L (single-pair Ethernet) could work in this distance, but the PHY chips (e.g., ADIN1100) are newer, less proven, and add cost. Standard CAN FD physical layer transceivers (TCAN1042VDRQ1) are mature, automotive-qualified, and inexpensive (£0.95 in volume).
4. **Power:** Ethernet switch + 4 PHYs = approximately 1.5 W of overhead. CAN FD transceivers: 4 × 7 mA × 5 V = 140 mW. The 1.36 W difference is meaningful on a battery-powered system.

### 5.3 RS-485 (Multi-drop) Alternative for All Boards

RS-485 multi-drop at 1 Mbps is technically feasible and simpler than CAN FD (no frame framing overhead, simpler collision detection via CSMA).

Rejection reasons for primary inter-board bus:
1. **No native fault isolation:** CAN FD's error confinement (bus-off state) isolates a faulty node from the bus automatically. A faulty RS-485 node that drives the line continuously will jam the entire bus. Detecting and recovering from such a fault requires firmware arbitration that duplicates the hardware mechanism CAN FD already provides.
2. **No frame integrity:** CAN FD frames include a 17-bit or 21-bit CRC (dependent on payload size) with bit stuffing. RS-485 relies on the UART framing (stop/start bits), providing much weaker error detection. For a safety-related command bus, the stronger CAN FD CRC is preferred.
3. **Data rate:** At 115200 baud (practical reliable limit for long cable RS-485), the latency for a 64-byte CAN FD equivalent frame would be 5.6 ms per frame. At 1 Mbps CAN FD, a 64-byte frame takes 0.5 ms. The latency difference matters for real-time lamp switching synchronisation between two LSO-100 units at opposite approaches.

**Why RS-485 for field LPI-100 cable:**  
RS-485 is retained for the field cable link to LPI-100 because:
- Cable runs up to 30 m (CAN FD at 4 Mbps data rate is limited to approximately 30 m with stub length < 5 m — marginal for field deployment)
- Simpler field cable (2-wire + power, no differential CAN pair impedance requirement on unshielded cable)
- The LPI-100 data rate requirement is low (push-button state + audible command = < 100 bytes/second)
- The LPI-100 is not on the safety inhibit path — even if the field cable fails, the LCU-100 safety supervisor will assert INHIBIT after 200 ms (no heartbeat from LPI-100 node), which produces the correct safe state (no pedestrian green phase)

### 5.4 CAN FD vs Classic CAN 2.0B

Classic CAN (2.0B, max 1 Mbps) was considered for cost saving (transceivers £0.25 cheaper per node vs CAN FD).

Rejection reason:  
The BQ25798 charger telemetry and BQ76952 battery monitor data require periodic transfers of 48–64 bytes per frame. Classic CAN is limited to 8 bytes per frame, requiring 6–8 frames per telemetry message. CAN FD supports up to 64 bytes payload in a single frame, reducing bus utilisation and simplifying the application message structure. At the planned heartbeat + telemetry rates, Classic CAN bus utilisation would exceed 40% — leaving insufficient headroom for fault reporting bursts. CAN FD at 4 Mbps data phase reduces nominal bus utilisation to approximately 5%.

---

## 6. Battery Management: BQ76952 vs Discrete Analogue Front-End

### 6.1 Selected Device

**Selected:** Texas Instruments BQ76952PFBR (16-series battery monitor with integrated protection)

### 6.2 Discrete Analogue Front-End Alternative

A common approach in lower-cost battery systems is to use discrete components:
- Cell voltage divider networks → ADC inputs on MCU
- Balancing: individual resistors + MOSFETs driven by MCU GPIO
- Overcurrent detection: shunt resistor + comparator (LM393 or similar)
- Overtemperature: NTC thermistor + comparator
- Protection FETs: N-channel MOSFETs driven by MCU GPIO

For a 4S LiFePO4 pack, this requires:
- 4 × precision voltage divider networks (resistors, 0.1% tolerance)
- 4 × analogue isolators or instrumentation amplifiers for cell-referenced measurements (e.g., AD8221 or INA128)
- 4 × balancing resistors (2 Ω, 3 W) + 4 × P-channel MOSFETs + 4 × gate pull-up resistors
- 1 × shunt resistor + 1 × current-sense amplifier (INA282 or equivalent)
- 2 × comparators (LM393 dual) for OV/UV detection
- 2 × NTC thermistors + 2 × comparators for temperature
- 4 × FET gate drive circuits for protection

**Comparison: BQ76952 vs discrete**

| Capability                        | BQ76952                                 | Discrete (4S)                            |
|-----------------------------------|-----------------------------------------|------------------------------------------|
| Cell voltage accuracy             | ±1 mV (16-bit differential ADC)        | ±10 mV (12-bit MCU ADC + divider error) |
| Balancing current                 | 20 mA (integrated, programmable)       | 1.5 A resistive (external 2 Ω, 3 W)     |
| Balancing control                 | Autonomous (OCV-based, no MCU needed)  | MCU-controlled (MCU fault → no balance) |
| Overcurrent response time         | < 1 ms (dedicated fast comparator)     | 5–20 ms (MCU ADC polling)               |
| Short circuit response            | < 200 µs (separate SC threshold)       | Not achievable with MCU polling          |
| Overvoltage/undervoltage          | Hardware latch-off, independent of MCU | MCU firmware required                    |
| Communication                     | I2C (standard library available)        | Custom MCU ADC + GPIO code              |
| Component count (4S)              | 1 IC + shunt resistor + 2 FETs          | 30+ discrete components                 |
| PCB area                          | 10 × 10 mm (WQFN-76)                   | Approx. 80 × 40 mm                      |
| Unit cost (2025)                  | £6.40                                   | £8.50 (30+ parts, estimated)            |

### 6.3 Decision Rationale

The BQ76952 is not only cheaper than the discrete equivalent but provides substantially better safety characteristics:

1. **Hardware protection independent of MCU:** The BQ76952's overcurrent and short-circuit protection acts in hardware (< 1 ms response) regardless of MCU state. A MCU watchdog reset that takes 100 ms leaves the battery unprotected during that window in a discrete system. With BQ76952, the protection FETs remain controlled by the IC's internal protection logic throughout the MCU reset cycle.

2. **Cell voltage accuracy for SOC estimation:** The ±1 mV accuracy of the BQ76952's 16-bit differential ADC is critical for the Impedance Track algorithm in the companion BQ28Z610 fuel gauge. The discrete solution's ±10 mV accuracy (after considering divider resistor tolerance and MCU ADC non-linearity) introduces ±5% SOC error at the top and bottom of the LiFePO4 voltage window (3.2–3.65 V/cell), where the OCV-SOC curve is steep.

3. **Balancing during charging:** Passive top-balancing requires individually monitoring each cell's OCV at end of charge and applying balancing current to the highest-voltage cell. The BQ76952 performs this autonomously without MCU involvement. In the discrete architecture, a firmware bug in balancing logic could lead to cell imbalance, reducing pack capacity and lifetime.

4. **Regulatory compliance:** The LPB-100 is classified as a lithium battery management system for professional use. The BQ76952 has been characterised for compliance testing with UN 38.3 (battery transport), IEC 62619 (stationary battery safety), and has TI application notes for UL 2054 compliance — all directly transferable to the Lumina design evidence package. Building equivalent evidence for a discrete analogue front-end from first principles is significantly more burdensome.

---

## 7. Operating Temperature Range: -30°C to +70°C Justification

### 7.1 Requirement Derivation

The Lumina controller is deployed at UK roadworks sites throughout the year. The enclosure (IP66 rated polycarbonate) provides limited thermal mass but is not climate-controlled. The thermal environment determines the component temperature range specification.

**Minimum ambient temperature:**  
UK winter extremes: −15°C recorded at ground level in most UK locations (absolute record −27°C in Scotland). Controller enclosure provides minimal insulation. Allowing for an additional −15°C margin to cover continental European deployments (Germany, Scandinavia) where product may be sold: **−30°C minimum ambient**.

**Maximum ambient temperature:**  
Solar loading on dark enclosure surface on a hot summer day: ambient air 35°C + solar gain through enclosure wall can add 20–30°C to internal temperature at worst case. IEC 60721-3-3 Class 3K3 (outdoor sheltered equipment) specifies +40°C maximum air ambient; Lumina applies an additional +30°C for solar loading and self-heating: **+70°C maximum component temperature**.

### 7.2 Component Temperature Grade Mapping

| Component Grade        | Temperature Range    | Used On                                              |
|-----------------------|----------------------|------------------------------------------------------|
| Commercial            | 0°C to +70°C        | Not used — does not cover minimum ambient            |
| Industrial            | −40°C to +85°C      | STM32H743ZIT6, STM32G071RBT6, MB85RS4MT, TCAN1042VDRQ1 |
| Automotive (AEC-Q100/Q101) | −40°C to +125°C | BTS7030-2EPA (AEC-Q101), PSMN1R4-40YLD (AEC-Q101) |
| Extended industrial   | −40°C to +105°C     | BQ76952, BQ25798, LMR54406XYFPR                     |

All selected components meet the −30°C to +70°C system requirement with margin. The −40°C lower rating of all industrial components provides a 10°C guard band below the −30°C system limit.

### 7.3 Components Requiring Special Attention at Temperature Extremes

**MLCC capacitors (X7R vs X5R vs C0G):**

Ceramic capacitors in bypass and filter applications must maintain sufficient capacitance across the temperature range. The derating of X5R at −30°C is significant:
- X5R specification: ±15% capacitance from −55°C to +85°C. At −30°C, capacitance may be at −15% of nominal.
- X7R specification: ±15% from −55°C to +125°C — more stable over the operating range
- C0G (NP0): ±30 ppm/°C — stable, used for timing-critical capacitors (RC debounce, oscillator)

**Lumina policy:** All decoupling and filter capacitors on signal paths use X7R dielectric (or better). X5R is acceptable for bulk supply decoupling (100 µF+ output caps) where the 15% variation has negligible impact on rail stability. C0G is mandatory for RC time-constant circuits.

**LiFePO4 battery charging at low temperature:**  
LiFePO4 cells should not be charged below 0°C (lithium plating risk). The BQ25798 receives temperature data from the PCT2075DP board sensor and from the BQ76952 cell temperature thermistors. Charging is suspended automatically when battery temperature < 5°C. This is implemented in BQ25798 hardware (TS pin thermal model configured via I2C) without requiring MCU intervention, ensuring the protection remains active even during MCU firmware update.

**Crystal oscillator start-up at −30°C:**  
The Abracon ABLS-25.000MHZ-B4-T HSE crystal is specified down to −40°C. At low temperatures, the equivalent series resistance (ESR) of quartz crystals increases. If the oscillator drive current is insufficient to overcome the increased ESR, the crystal fails to start. STM32H743's HSE oscillator drive strength is configurable (low/medium/high) — the LCU-100 firmware sets HIGH drive strength for reliable start-up at −30°C at the cost of slightly increased EMI from the crystal circuit. This setting is validated during thermal qualification at −35°C (5°C margin below −30°C system minimum).

**Connector contacts at −30°C:**  
Molex Micro-Fit 3.0 connectors are specified to −40°C. Connector mating force increases at low temperatures due to plastic stiffening. The assembly drawing notes that connectors should be mated at temperatures above −20°C where possible; if the unit must be connected at −30°C, the connector latching mechanism must be verified to operate correctly with gloved hands (relevant for site use in winter).

---

*Document end. For component data sheets and qualification test reports, see LUM-HW-COMP-001 Appendix A (stored in PLM document vault).*
