# Lumina PCB Stackup and Layout Rules

**Document Number:** LUM-HW-PCB-001  
**Revision:** A  
**Date:** 2026-06-18  
**Applies To:** LCU-100, LSO-100, LPB-100, LPI-100  
**Reference Standards:** IPC-2221B (Generic Standard on Printed Board Design), IPC-2581C (Board Design Transfer Format), IPC-6012E (Qualification and Performance Specification for Rigid Printed Boards)

---

## Table of Contents

1. [LCU-100 — 6-Layer Stackup](#1-lcu-100--6-layer-stackup)
2. [LSO-100 — 4-Layer Stackup](#2-lso-100--4-layer-stackup)
3. [LPB-100 — 4-Layer Stackup](#3-lpb-100--4-layer-stackup)
4. [LPI-100 — 4-Layer Stackup](#4-lpi-100--4-layer-stackup)
5. [Impedance Control Requirements](#5-impedance-control-requirements)
6. [Thermal Via Requirements](#6-thermal-via-requirements)
7. [RF and Antenna Keepout Rules](#7-rf-and-antenna-keepout-rules)
8. [High-Current Path Routing](#8-high-current-path-routing)
9. [Minimum Trace Widths and Clearances](#9-minimum-trace-widths-and-clearances)
10. [Board Dimensions and Connector Placement](#10-board-dimensions-and-connector-placement)
11. [Conformal Coating Requirements](#11-conformal-coating-requirements)

---

## 1. LCU-100 — 6-Layer Stackup

### 1.1 Layer Stack Definition

The LCU-100 uses a 6-layer stackup to accommodate the high routing density of the STM32H743ZIT6 (LQFP-144) and associated peripherals, while providing continuous GND reference planes for controlled-impedance high-speed traces (USB 2.0, CAN FD, GNSS SPI).

**Total board thickness:** 1.6 mm ± 0.15 mm  
**Base material:** FR4 (Tg ≥ 150°C, Td ≥ 300°C, CAF-resistant per IPC-4101C /126)  
**Laminate dielectric constant:** εr = 4.2 ± 0.1 at 1 GHz (Isola IS410 or equivalent)  
**Copper surface finish:** ENIG (Electroless Nickel Immersion Gold) per IPC-4552A, Ni 3–6 µm, Au 0.05–0.1 µm  

| Layer | Name      | Function                                    | Copper Weight | Nominal Thickness |
|-------|-----------|---------------------------------------------|---------------|-------------------|
| L1    | TOP       | Component placement, signal routing, RF CPW | 1 oz (35 µm)  | 35 µm             |
| —     | Prepreg 1 | L1–L2 dielectric (2116 style, 2× sheets)    | —             | 200 µm            |
| L2    | GND1      | Continuous ground plane (reference for L1)  | 1 oz (35 µm)  | 35 µm             |
| —     | Core 1    | L2–L3 dielectric (7628 style)               | —             | 400 µm            |
| L3    | SIG2      | Internal signal routing (sensitive signals) | 0.5 oz (17 µm)| 17 µm             |
| —     | Prepreg 2 | L3–L4 dielectric (1080 style)               | —             | 100 µm            |
| L4    | PWR       | Power planes (split: 3.3V, 5V, 3.8V zones) | 1 oz (35 µm)  | 35 µm             |
| —     | Core 2    | L4–L5 dielectric (7628 style)               | —             | 400 µm            |
| L5    | GND2      | Continuous ground plane (reference for L6)  | 1 oz (35 µm)  | 35 µm             |
| —     | Prepreg 3 | L5–L6 dielectric (2116 style, 2× sheets)    | —             | 200 µm            |
| L6    | BOT       | Component placement (passive, connectors)   | 1 oz (35 µm)  | 35 µm             |

**Total copper + dielectric thickness:** 35+200+35+400+17+100+35+400+35+200+35 = 1492 µm ≈ 1.5 mm plus solder mask and surface finish = 1.6 mm.

### 1.2 Power Plane Splits (L4)

L4 is divided into the following copper pours with minimum 0.5 mm clearance between split boundaries:

| Zone Name | Voltage | Coverage Area (approx.)            |
|-----------|---------|------------------------------------|
| VCC3V3    | 3.3 V   | Centre region, MCU and peripherals |
| VCC5V     | 5 V     | Left side, USB, CAN transceivers   |
| VCC3V8    | 3.8 V   | Top-right corner, LTE modem M.2    |
| VCC1V8    | 1.8 V   | Top-right corner, GNSS module      |

Split boundaries are routed away from high-speed signal via pads on L3 to prevent return current discontinuities. Any signal on L3 that must cross a power plane split boundary is first stitched to GND1 (L2) by a short GND via before and after the crossing, and the signal is re-referenced.

### 1.3 Layer Assignments for Signal Routing

| Signal Type                    | Preferred Layer | Notes                                              |
|-------------------------------|-----------------|-----------------------------------------------------|
| USB 2.0 differential pair      | L1              | CPW, referenced to GND1 (L2), 100 Ω differential  |
| CAN FD differential pair       | L1 or L6        | Referenced to adjacent GND plane, 100 Ω diff      |
| GNSS SPI (1.8 V logic)        | L3              | Away from top-side switching noise                 |
| STM32H743 GPIO, UART, I2C     | L1 and L6       | Short runs near MCU                                |
| Clock traces (XTAL, PLL)      | L3              | Maximum shielding from external interference       |
| Power supply traces           | L4 (planes)     | Short vertical connections via vias                |

### 1.4 Via Structures

**Standard signal via:** 0.2 mm drill / 0.4 mm pad / 0.6 mm annular ring clearance. Used for all signal interconnects between layers.

**Microvia (laser drill):** Not required for LCU-100. Minimum via drill 0.2 mm is achievable with mechanical drilling on a 6-layer board at this pitch.

**Thermal via (large pads):** 0.3 mm drill / 0.6 mm pad, no annular ring solder mask opening. Used under STM32H743 exposed pad and TPS7A4700RGWT. See Section 6.

**GND stitching vias:** Placed at 2.5 mm grid across GND1 and GND2 planes to ensure low-impedance return path continuity. Additional stitching vias placed adjacent to any gap or slot in a GND plane within 0.5 mm of the gap edge.

---

## 2. LSO-100 — 4-Layer Stackup

### 2.1 Layer Stack Definition

The LSO-100 uses a 4-layer stackup. The outer copper layers are 2 oz to handle the high output trace currents (up to 5 A continuous per channel to the lamp connector).

**Total board thickness:** 1.6 mm ± 0.15 mm  
**Base material:** FR4 (Tg ≥ 170°C per IPC-4101C /129 — higher Tg selected due to power dissipation from BTS7030-2EPA devices)  
**Copper surface finish:** ENIG  

| Layer | Name  | Function                                            | Copper Weight | Nominal Thickness |
|-------|-------|-----------------------------------------------------|---------------|-------------------|
| L1    | TOP   | Component placement, high-current output traces     | 2 oz (70 µm)  | 70 µm             |
| —     | Prepreg | L1–L2 dielectric (7628 style, 1× sheet)           | —             | 200 µm            |
| L2    | GND   | Continuous ground plane                             | 1 oz (35 µm)  | 35 µm             |
| —     | Core  | L2–L3 dielectric (FR4 core)                        | —             | 900 µm            |
| L3    | PWR   | 12 V power bus, isolated 5 V CAN supply             | 1 oz (35 µm)  | 35 µm             |
| —     | Prepreg | L3–L4 dielectric (7628 style, 1× sheet)           | —             | 200 µm            |
| L4    | BOT   | Signal routing, BTS7030 SPI, INA219 I2C            | 2 oz (70 µm)  | 70 µm             |

**Total:** 70+200+35+900+35+200+70 = 1510 µm plus solder mask = 1.6 mm.

### 2.2 Output Trace Routing (L1, 2 oz copper)

Each BTS7030-2EPA output (drain pin to connector) is routed as a 5 mm minimum width trace on L1 (2 oz copper). Trace width is calculated per IPC-2221B Table 6-1 (external conductors):

At 5 A continuous, 2 oz copper (70 µm thickness), 10°C temperature rise above ambient:  
Required trace width = 5 A / (k × ΔT^0.44 × A^0.725) — using IPC-2221 nomograph → 1.5 mm minimum.

5 mm width provides significant margin (rated capacity ≈ 16 A at 2 oz, 10°C rise) to minimise resistive voltage drop across the trace. Total trace resistance at 5 mm wide, 50 mm long, 2 oz copper = (ρL)/(wt) = (1.72e-8 × 0.05)/(0.005 × 0.000070) = 2.5 mΩ. At 5 A: V_drop = 12.5 mV — acceptable.

Minimum clearance between adjacent output channel traces: 1.0 mm (IPC-2221 Class B, 50 V working voltage, pollution degree 2 → 0.5 mm minimum; 1.0 mm applied for added creepage margin in harsh environments).

### 2.3 CAN FD and SPI Routing (L4)

CAN FD differential pair on L4: 100 Ω differential, 50 Ω single-ended. Target trace width/spacing for 100 Ω differential on L4 with L3 (PWR plane) as reference: width = 0.15 mm, space = 0.2 mm, calculated for εr = 4.2, trace height above reference = 200 µm prepreg + 35 µm copper + 70 µm outer = approximately 305 µm dielectric separation.

SPI traces to BTS7030-2EPA: 0.2 mm width (signal, no impedance control required), routed with maximum 10 mm length between LSO MCU and BTS7030 CS/CLK/MISO/MOSI pins. Matched length within ±0.5 mm for SPI_CLK and SPI_MOSI pairs.

---

## 3. LPB-100 — 4-Layer Stackup

### 3.1 Layer Stack Definition

The LPB-100 uses 2 oz copper throughout all layers due to the high battery charge/discharge currents (up to 20 A). The board must sustain these currents continuously without exceeding 30°C temperature rise on copper traces.

**Total board thickness:** 1.6 mm ± 0.15 mm  
**Base material:** FR4 (Tg ≥ 170°C)  
**Copper surface finish:** ENIG. Heavy copper (2 oz) on all layers requires extended ENIG process (additional Ni activation step) — advise PCB manufacturer at Gerber submission.

| Layer | Name  | Function                                            | Copper Weight | Nominal Thickness |
|-------|-------|-----------------------------------------------------|---------------|-------------------|
| L1    | TOP   | BQ76952, BQ25798, LTC4412, power connectors         | 2 oz (70 µm)  | 70 µm             |
| —     | Prepreg | L1–L2 (2116 style, 2× sheets)                    | —             | 200 µm            |
| L2    | GND   | Continuous ground plane, thermal heat spread        | 2 oz (70 µm)  | 70 µm             |
| —     | Core  | L2–L3 core                                         | —             | 900 µm            |
| L3    | PWR   | Battery positive bus, charger bus, 12 V output bus  | 2 oz (70 µm)  | 70 µm             |
| —     | Prepreg | L3–L4 (2116 style, 2× sheets)                    | —             | 200 µm            |
| L4    | BOT   | SMBus, I2C, low-current signal routing              | 2 oz (70 µm)  | 70 µm             |

### 3.2 Battery Current Path Design

The battery charge/discharge current path (from J1 battery connector to J2 system output connector) must carry up to 20 A continuous. On L3 (PWR plane), the battery positive bus is routed as a full-width copper pour on one half of the layer (left side = battery input; right side = 12 V output bus), separated by a 2 mm gap at the BMS FET crossing point.

**PSMN1R4-40YLD FET pad design:** The LFPAK56 package has a large drain paddle on the bottom. A 20 × 15 mm copper island on L1 connects all drain pads to the battery bus. An equivalent island on L1 at the source side connects to the output bus. The two islands are connected through the FET channel only. Four thermal vias (0.5 mm drill, HASL filled) in a 2 × 2 array under each LFPAK56 package connect L1 to L3 for reduced thermal resistance.

### 3.3 Shunt Resistor Placement (Isabellenhütte BVR-Z-R0005-1.0)

The current shunt is a 4-terminal (Kelvin) component. Placement rules:
- Place on L1, in the main current path between battery connector J1 and BMS discharge FET
- Force terminals (F+ and F−) carry high current — connect with minimum 10 mm wide copper pours on L1 and L3
- Sense terminals (S+ and S−) route to BQ76952 sense inputs on L4 via 0.2 mm traces — these traces must NOT carry load current
- Sense trace length matched within ±2 mm to minimise differential pickup
- Sense traces routed as a twisted pair on L4, minimum 5 mm away from high-current traces on L1 and L3

---

## 4. LPI-100 — 4-Layer Stackup

### 4.1 Layer Stack Definition

The LPI-100 has a simpler signal environment (low-speed digital, audio frequency analogue) and moderate current paths (≤ 2 A). Standard 1 oz copper with a conventional 4-layer stack is sufficient.

**Total board thickness:** 1.6 mm ± 0.15 mm  
**Board dimensions:** 80 × 60 mm  
**Base material:** FR4 standard (Tg ≥ 130°C)  
**Copper surface finish:** ENIG  

| Layer | Name  | Function                                          | Copper Weight | Nominal Thickness |
|-------|-------|---------------------------------------------------|---------------|-------------------|
| L1    | TOP   | MCU, PAM8008, DRV8833, push-button circuit       | 1 oz (35 µm)  | 35 µm             |
| —     | Prepreg | L1–L2 (2116 style)                             | —             | 200 µm            |
| L2    | GND   | Continuous ground plane                           | 1 oz (35 µm)  | 35 µm             |
| —     | Core  | L2–L3                                            | —             | 900 µm            |
| L3    | PWR   | 5 V power plane, 12 V power pour                 | 1 oz (35 µm)  | 35 µm             |
| —     | Prepreg | L3–L4 (2116 style)                             | —             | 200 µm            |
| L4    | BOT   | CAN FD transceiver, connectors                   | 1 oz (35 µm)  | 35 µm             |

### 4.2 Audio Routing Rules

The PAM8008 Class-D amplifier output and speaker wiring are treated as high-frequency signals at the switching frequency (hundreds of kHz). Routing rules for audio traces:

- PAM8008 output pins (BTL+ and BTL−) to speaker connector: maximum 30 mm trace length on L1, no vias in this path
- Minimum trace width 0.8 mm for speaker output traces (1 A peak Class-D current)
- Keep BTL+ and BTL− traces parallel with 0.5 mm spacing (balanced routing to reduce EMI)
- No sensitive analogue (tone filter RC network) traces within 5 mm of the BTL output traces
- Place 10 nF / 50 V C0G snubber capacitors across the speaker connector pins (within 5 mm of connector pads) to limit dV/dt on long speaker cable

### 4.3 Push-Button Cable ESD Rules

The cable connection to the external push-button assembly (J2, 4-way Molex Micro-Fit 3.0) is the primary ESD entry point for the LPI-100. Layout rules:
- PRTR5V0U2X ESD clamp placed within 3 mm of J2 pin 1 (button+ signal)
- No ground-plane void under J2 or ESD clamp — continuous GND plane on L2 provides low-impedance ESD return path
- RC debounce filter (47 Ω + 100 nF) placed between PRTR5V0U2X output and SN74LVC1G17 input within 10 mm path length
- Any trace to J2 must not run adjacent to CAN FD traces — minimum 3 mm separation from TCAN1042 differential pair

---

## 5. Impedance Control Requirements

### 5.1 Single-Ended 50 Ω (Microstrip)

Target: 50 Ω ± 10%  
Applications: RF coax footprint to LTE SMA (LCU-100 L1), GNSS antenna trace (LCU-100 L1)

**LCU-100 L1 (50 Ω microstrip, L1 to L2 reference):**  
εr = 4.2, trace height above GND plane = 200 µm (prepreg 1 thickness), copper thickness = 35 µm  
Required trace width: Using microstrip formula Z₀ = (87/√(εr+1.41)) × ln(5.98h/(0.8w+t)):  
Solving for Z₀ = 50 Ω, h = 200 µm, t = 35 µm, εr = 4.2 → w ≈ 370 µm (0.37 mm)

Nominal trace width specified to PCB manufacturer: **0.375 mm ± 0.025 mm** (controlled impedance requirement, referenced to 50 Ω, measured per IPC-2141A, tolerance ±10%).

### 5.2 Differential 100 Ω (Microstrip)

Target: 100 Ω differential ± 10%  
Applications: USB 2.0 D+/D− (LCU-100 L1), CAN FD CANH/CANL (LCU-100 L1, LSO-100 L4), GNSS SPI (LCU-100 L3)

**LCU-100 L1 (100 Ω differential microstrip, L1 to L2):**  
Using edge-coupled microstrip differential impedance formula:  
For εr = 4.2, h = 200 µm, t = 35 µm, target Zdiff = 100 Ω:  
Width = 0.20 mm, space = 0.20 mm (edge-to-edge gap between traces)

Specified to manufacturer: **width 0.20 mm, space 0.20 mm ± 0.02 mm** (controlled impedance, 100 Ω differential, ±10%).

**LSO-100 L4 (100 Ω differential microstrip, L4 to L3 reference):**  
Same dielectric height (200 µm prepreg) and εr. Same specification applies.

### 5.3 IPC-2581 Impedance Annotation

All controlled-impedance traces shall be annotated in the IPC-2581C board data file using the `<Impedance>` element. The Gerber layer notes and netlist must identify each controlled-impedance net name (USB_DP, USB_DM, CAN_H, CAN_L, RF_ANT, GNSS_ANT) for the PCB manufacturer's impedance test coupon validation.

The manufacturer must provide impedance test coupons on the panel border, measured using a TDR (time-domain reflectometry) instrument with 10 ps rise time. Test coupon results must be included in the incoming inspection records.

---

## 6. Thermal Via Requirements

### 6.1 BTS7030-2EPA (LSO-100)

The BTS7030-2EPA PG-DSO-14 package has a large bottom-side exposed thermal pad (5.7 × 5.9 mm). Thermal design must achieve θJA ≤ 20°C/W to maintain Tj < 125°C at maximum ambient of +70°C and 1.5 W device dissipation.

**Via array specification:**
- Via drill: 0.3 mm (minimum mechanically drillable without specialist capability)
- Via pad: 0.6 mm on L1, tented on L2 underside (prevent solder wicking)
- Array: 4 × 4 = 16 vias within the exposed pad footprint
- Via pitch: 1.2 mm (fits 4 × 4 in 5.7 × 5.9 mm pad with 0.5 mm edge clearance)
- Via plating: minimum 25 µm copper wall thickness (standard IPC-6012 Class 2 via plating)
- Connection: vias connect L1 exposed pad copper to L2 GND plane — the GND plane acts as a heat spreader

Additional thermal management: 50 × 50 mm copper flood on L1 adjacent to BTS7030-2EPA, connected to GND through stitching vias. A Bergquist GP3000S30 (3.0 W/m·K, 0.25 mm thickness) thermal interface pad is applied between the PCB bottom surface and the aluminium chassis wall.

Calculated θJA with chassis:  
θJC (package) = 3.3°C/W  
θvia (16 × 0.3 mm Cu vias through 0.9 mm board) = 1/(16 × π × r² × λCu / L) ≈ 8°C/W  
θinterface (Bergquist GP3000S30, 50 × 50 mm) = 0.25/(3.0 × 0.0025) = 0.033°C/W  
θchassis = 5°C/W (aluminium wall, natural convection)  
Total θJA ≈ 3.3 + 8 + 0.033 + 5 ≈ 16.3°C/W — acceptable (limit 20°C/W)

### 6.2 TPS7A4700RGWT (LCU-100)

The TPS7A4700RGWT VQFN-20 package exposed pad is 2.0 × 2.0 mm.

**Via array:** 3 × 3 = 9 vias, 0.3 mm drill, 0.6 mm pad, within 2.0 × 2.0 mm. Vias connect to L2 GND plane. An additional 10 × 10 mm GND copper pour on L1 adjacent to the regulator provides added thermal spreading.

At maximum load (12 V in, 5 V out, 1 A): P = (12 − 5) × 1 = 7 W. This is at the limit of PCB-only thermal management. Preferred operating point: maximum 500 mA from TPS7A4700RGWT; auxiliary loads above 500 mA use a separate lower-dropout path.

### 6.3 PSMN1R4-40YLD (LPB-100)

The LFPAK56 package (5 × 6 mm, bottom-side drain pad).

**Via array:** 4 × 4 = 16 vias, 0.5 mm drill (larger drill for lower resistance), 0.9 mm pad, within the 5 × 6 mm footprint. Vias connect drain pad on L1 to L3 power plane (battery bus) — this serves the dual purpose of electrical current path and thermal dissipation.

At 20 A continuous: P = I² × RDS(on) = 400 × 0.0014 = 0.56 W — minimal thermal challenge. Via array is sized for current carrying rather than thermal.

---

## 7. RF and Antenna Keepout Rules

### 7.1 LTE Antenna Keepout (LCU-100)

The Quectel EC21-A LTE modem on M.2 socket and its SMA antenna connector require the following keepout zones:

**M.2 module area (top-right corner, approx. 45 × 35 mm):**
- L1: No signal traces permitted within 5 mm of M.2 socket pads (exception: 50 Ω CPW RF feed trace)
- L2: GND plane continuous (no splits, no voids), stitched at 2 mm grid to reduce RF return impedance
- L3: No signal routing — GND fill only
- L4: No power plane splits within module area — contiguous power pour only
- L5: GND plane continuous
- L6: No components within 5 mm of M.2 socket perimeter

**SMA connector keepout (antenna edge):**
- 10 mm radius clearance from SMA centre pin pad — no copper on any layer except the CPW ground plane
- The 50 Ω CPW trace (0.375 mm wide, 0.2 mm gap to GND coplanar pour) must maintain unbroken GND reference for its full length from SMA footprint to M.2 antenna pad

### 7.2 GNSS Antenna Keepout (LCU-100)

The u-blox SAM-M10Q module (9.6 × 9.6 mm) incorporates a small patch antenna on its top surface. The SAM-M10Q must be placed with:

- 20 × 20 mm keepout zone on all layers below the antenna PCB — no copper except GND stitching
- Minimum 15 mm clearance from any metal structure (chassis wall, connector shell, cable lug) above the SAM-M10Q
- SAM-M10Q placed within 10 mm of the board edge nearest to sky view (top edge of LCU-100 when mounted in controller chassis with lid removed)
- No high-speed clocks or switching regulators within 20 mm of SAM-M10Q (switchmode noise at 2.1 MHz and harmonics can mask GPS L1 signal at 1575.42 MHz)

---

## 8. High-Current Path Routing

### 8.1 LSO-100 Output Traces

Six output channels, each up to 5 A continuous. Trace width on L1 (2 oz copper):
- Minimum width: 5 mm (calculated above, §2.2) — provides 16 A capacity with 10°C rise
- Preferred width: maximum available pour width after clearances
- No vias in the current path between BTS7030-2EPA drain pad and output connector — resistance of even a 0.5 mm via is measurable at 5 A
- Output connector (Molex Micro-Fit 3.0 or equivalent 8-way, J1) placed within 10 mm of BTS7030-2EPA to minimise total trace length

**Trace symmetry:** All 6 output channel traces shall have matched length within ±5 mm and matched impedance (same width and reference plane). Asymmetric traces cause unequal current sensing errors in the INA219 due to trace resistance offset.

### 8.2 LPB-100 Battery Bus

The battery bus on L3 (PWR plane) and L1 (component side) carries up to 20 A continuous (charge + discharge). Design rules:

- Battery connector J1 (Anderson SB50, 50 A rated, PCB-mount variant) placed at left board edge
- 12 V output connector J2 (same Anderson SB50) placed at right board edge
- Battery bus copper on L3: full 60 mm width pour from J1 to J2 minus component clearances — estimated current density < 0.5 A/mm² (well below 5 A/mm² limit for 2 oz copper, 10°C rise)
- Sense lines from Isabellenhütte BVR-Z shunt to BQ76952 and BQ28Z610: route on L4 (BOT), minimum 10 mm away from battery bus pours on L1 and L3 to avoid magnetically induced noise

### 8.3 LPB-100 Charger Output Traces

BQ25798 output (to LTC4412 ideal diode ORing, then to 12 V bus): up to 20 A at full MPPT charge current.

Trace specification: L1, 2 oz copper, 10 mm minimum width from BQ25798 SW output through inductor (Bourns SRR1260 series, rated 6 A — see power budget note: charger inductor is for 6 A continuous, 20 A figure is battery discharge current via separate path) to LTC4412 input.

---

## 9. Minimum Trace Widths and Clearances

### 9.1 IPC-2221B Class B Requirements

IPC-2221B Class B (Consumer/Industrial) applies to all Lumina boards. The following rules are the minimums; layout should use larger values where space permits.

| Parameter                          | IPC-2221B Class B Minimum | Lumina Minimum (applied) |
|------------------------------------|--------------------------|--------------------------|
| Conductor width (signal)           | 0.100 mm                 | 0.150 mm                 |
| Conductor width (power, ≤ 1 A)    | 0.200 mm                 | 0.300 mm                 |
| Conductor width (power, 1–5 A)    | IPC Table 6-1 per ampere | Per §2.2 calculation     |
| Conductor clearance (signal, ≤12V)| 0.100 mm                 | 0.150 mm                 |
| Conductor clearance (12V–25V)     | 0.200 mm                 | 0.300 mm                 |
| Conductor clearance (25V–50V)     | 0.400 mm                 | 0.500 mm                 |
| Via drill (minimum)                | 0.200 mm                 | 0.200 mm                 |
| Via annular ring (minimum)         | 0.050 mm                 | 0.100 mm                 |
| Hole to conductor clearance        | 0.330 mm                 | 0.400 mm                 |
| Board edge clearance (conductors)  | 0.500 mm                 | 1.000 mm                 |
| Board edge clearance (vias)        | 0.300 mm                 | 0.500 mm                 |
| Solder mask opening over via       | Tented (no opening)      | Tented (standard)        |
| Solder mask expansion (SMD pads)   | +0.050 mm per side       | +0.075 mm per side       |

### 9.2 Creepage and Clearance for Mains-Adjacent Voltages

The LPB-100 battery bus (up to 16.8 V charged LiFePO4) and solar input (up to 24 V) do not constitute mains voltages, but the following creepage rules are applied per IEC 60950-1 for working voltages 25–50 V in Pollution Degree 2:

- Minimum clearance (through air): 0.5 mm
- Minimum creepage (along surface): 1.0 mm
- Applied to all high-voltage copper adjacent to signal connectors on LPB-100

---

## 10. Board Dimensions and Connector Placement

### 10.1 Board Dimensions

| Board   | Length (X) | Width (Y) | Mounting Holes | Hole Diameter | Corner Radii |
|---------|------------|-----------|----------------|---------------|--------------|
| LCU-100 | 160 mm     | 100 mm    | 4 × M3         | 3.2 mm        | 2 mm         |
| LSO-100 | 140 mm     | 100 mm    | 4 × M3         | 3.2 mm        | 2 mm         |
| LPB-100 | 120 mm     | 80 mm     | 4 × M3         | 3.2 mm        | 2 mm         |
| LPI-100 | 80 mm      | 60 mm     | 4 × M3         | 3.2 mm        | 2 mm         |

All mounting holes are 3.2 mm diameter (clearance for M3 screw), with 1.5 mm annular copper ring on all layers connected to GND (chassis ground stitching). Hole centres placed 5 mm from nearest board edge on both axes.

### 10.2 Connector Placement Rules

**LCU-100:**
- J1 (Main CAN FD / Power header, 12-way Molex Micro-Fit 3.0): bottom edge, centred at X = 80 mm
- J3 (USB-C): left edge, 20 mm from bottom-left corner, recessed 0.5 mm from board edge for alignment with chassis cutout
- J5 (SWD debug): top edge, right side — internal access only, no chassis cutout required
- J6 (M.2 B-key LTE modem): top-right quadrant, parallel to top edge
- J7 (LTE SMA): top-right corner, 5 mm from right edge
- J8 (GNSS active antenna SMA): top edge, 15 mm from right edge
- J9 (Nano-SIM): top-right area, beside M.2 socket, with SIM ejection slot accessible via chassis cover

**LSO-100:**
- J1 (Lamp output connector, 12-way, 2 × 6 Molex Micro-Fit 3.0 dual-row): bottom edge, spanning full width — heavy-current connections exit from PCB bottom
- J2 (Power + CAN in, 8-way Micro-Fit): left edge
- Fuse holder (30 A ATO): top edge, near 12 V input, minimum 20 mm from connectors for accessibility

**LPB-100:**
- J1 (Battery connector, Anderson SB50): left edge, exposed for direct battery cable connection
- J2 (12 V system output, Anderson SB50): right edge
- J3 (Solar input, 4-way, rated 20 A): top edge, near BQ25798
- J4 (CAN FD / comms, 6-way Micro-Fit): bottom edge

**LPI-100:**
- J1 (Push-button assembly, 4-way Micro-Fit): top edge
- J2 (Speaker output, 2-way 2.54 mm pitch): top-right corner
- J3 (CAN FD / RS-485, 8-way Micro-Fit): bottom edge
- J4 (12 V power in, 2-way 2.54 mm pitch with polarised connector): left edge

### 10.3 Mating Envelope Requirements

All board-mounted connectors must have the following clearances for mating cable assembly:
- 25 mm straight cable exit clearance in the connector mating direction from the connector face
- 50 mm bend radius clearance for cables departing at angles (Micro-Fit 3.0 cables with strain relief)
- Anderson SB50 connectors (LPB-100) require 60 mm straight-pull clearance — confirmed against chassis drawing before finalising LPB-100 layout

---

## 11. Conformal Coating Requirements

### 11.1 Coating Material

All four Lumina PCBs receive conformal coating after in-circuit test (ICT) and functional test. The specified coating is:

**Humiseal 1B31** (acrylic, solvent-based) applied by selective spray coat to 50–75 µm dry film thickness, per IPC-CC-830B Class AR (Acrylic Resin).

Humiseal 1B31 was selected for:
- Temperature range: −65°C to +125°C
- Dielectric strength: > 1600 V/mil
- Water absorption: < 0.2%
- Fungal resistance: passes IPC-TM-650 2.6.1
- Repairability: soluble in standard solvents (acetone, toluene) for field repair
- UV fluorescence: visible under 365 nm UV lamp for inspection per IPC-A-610

### 11.2 Coating Mask Exclusion Zones

Conformal coating must NOT be applied to the following areas. These must be defined as coating exclusion zones in the assembly drawing (drawing number LUM-ASM-001 through 004):

**All boards:**
- All connector housings and mating faces (coating interferes with mating and can cause intermittent contacts)
- Test points (TP1–TPn as defined per board testability document LUM-TEST-001)
- Fuse holders (accessibility)
- Adjustable components (trimmer potentiometers, if any)
- Mounting hole annular rings (to ensure chassis GND contact)

**LCU-100 specific:**
- M.2 modem socket J6 (LTE module must be removable for firmware update and field replacement)
- Nano-SIM card holder J9 (card must be insertable/removable in field)
- USB-C connector J3 (mating face)
- SWD debug header J5 (field use by service technicians)
- ML2032 coin cell holder (cell must be replaceable)

**LPB-100 specific:**
- Anderson SB50 connector J1 and J2 flanges (2 mm clearance from connector shell edge)
- Battery management indicator LEDs (D1–D4) if mounted with clear epoxy — coating diffuses LED light, reducing visibility. Apply coating to LED side walls only, not to lens

**LSO-100 specific:**
- ATO blade fuse holder top surface (fuse must be replaceable in field without solvent wash)
- Output terminal blocks or connector J1 (high-voltage terminals must remain accessible)

### 11.3 Coating Inspection Requirements

Post-coating inspection per IPC-A-610 Section 10.6.4 (Class 2 product):
1. Visual inspection under white light: no blisters, delamination, or uncured areas
2. UV inspection at 365 nm: coating coverage verified over entire required area, exclusion zones confirmed clear
3. Film thickness measurement (eddy-current probe or cross-section): 50–75 µm at minimum 3 points per board
4. Adhesion test (tape pull per ASTM D3359): performed on coating sample board per batch, not on production boards

Coating records (batch number, operator ID, date, cure time, film thickness readings) are retained with each board serial number in the production traveller document.

---

*Document controlled by Lumina Engineering. Changes to stackup or coating specification require ECO (Engineering Change Order) with updated IPC-2581 files and re-verification of impedance test coupons.*
