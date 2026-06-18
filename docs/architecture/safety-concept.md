# Lumina LCU-100 Preliminary Safety Concept

**Document Reference:** LCU-100-SC-001  
**Revision:** 0.3  
**Status:** Preliminary — Engineering Design Guidance Only  
**Date:** 2026-06-18  
**Author:** Lumina Safety Engineering  
**Classification:** Confidential — Engineering Internal

---

> **IMPORTANT NOTICE**
>
> This document is a **preliminary safety concept** prepared to guide engineering design decisions during the development of the LCU-100 platform. It is **not** a formal IEC 61508 safety case, and it does **not** constitute a SIL claim or Functional Safety Assessment.
>
> A full IEC 61508 Part 2 safety lifecycle assessment, including quantified SIL determination, probability calculations (PFH / PFD), and FMEA, is required before the LCU-100 may be submitted for highway authority type approval or used in a live highway deployment. TOPAS 2540A approval testing by an accredited certifying body is also required before deployment.
>
> This document should be used by engineers to understand the intended safety architecture and design rationale. It must not be cited as evidence of compliance with any safety standard.

---

## Table of Contents

1. Safety Purpose and Scope
2. Hazard Identification (Simplified HAZOP)
3. Safety Architecture
4. Conflict Matrix Definition
5. Intergreen Timing (UK TOPAS Requirement)
6. Fail-Safe States
7. Items Outside This Safety Concept

---

## 1. Safety Purpose and Scope

### 1.1 System Description

The Lumina LCU-100 is a portable traffic signal controller intended for use in temporary traffic management on UK highways, including road works, incident management, and event traffic control. It controls up to four signal heads (approaches) and associated pedestrian crossing points via CAN FD-connected LSO-100 signal output modules.

### 1.2 Operational Context

Temporary traffic signals operate in a single-lane working or contra-flow configuration. Vehicles approach from opposing directions and are held at red while conflicting traffic passes. The primary safety function is to ensure that vehicles on conflicting approaches are never simultaneously given a green aspect.

Unlike permanent signal installations, temporary signals:
- Are installed by works teams rather than specialist contractors, increasing the risk of configuration errors.
- Operate in varying weather and lighting conditions.
- May be battery-powered, introducing the risk of controlled shutdown under low-battery conditions.
- Are connected by cable to signal heads that may be damaged by site vehicles.

### 1.3 Safety Goals

The primary safety goal of the LCU-100 is:

**SG-1:** Prevent the simultaneous energisation of conflicting signal aspects (green vs green on opposing approaches).

Secondary safety goals are:

**SG-2:** Ensure a defined minimum intergreen period (amber clearance + all-red) between competing greens on any pair of approaches.

**SG-3:** Maintain a defined, observable signal state at all times; avoid dark or undefined signal head behaviour.

**SG-4:** On loss of control unit function (watchdog expiry, power failure), achieve a fail-safe all-red state and maintain it.

**SG-5:** Provide a controlled shutdown sequence under battery brownout conditions to avoid an abrupt, undefined state change.

### 1.4 Scope of This Document

This document covers:

- Hazard identification for the LCU-100 in typical UK temporary signal operation.
- The safety architecture implemented in LCU-100 hardware and firmware to address identified hazards.
- The conflict matrix and intergreen timing requirements enforced by the Safety MCU.
- Fail-safe behaviour definitions.

This document does **not** cover pedestrian protection (LPI-100), RF synchronisation safety (Phase 2), or formal SIL quantification. See Section 7 for items explicitly excluded.

---

## 2. Hazard Identification (Simplified HAZOP)

The following hazard table was developed using a simplified HAZOP (Hazard and Operability Study) methodology. Guide words applied: "No signal", "Wrong signal", "Too long", "Too short", "Out of sequence". The risk level uses a qualitative matrix: Severity × Frequency, with levels Low / Medium / High / Critical.

This table represents a subset of hazards for engineering design guidance. It is not a complete hazard log and has not been subject to formal IEC 61508 hazard and risk analysis.

| Hazard ID | Description | Potential Causes | Severity | Frequency | Risk Level | Required Safeguard |
|---|---|---|---|---|---|---|
| **H1** | Conflicting green aspects on opposing approaches (simultaneous green) | Software bug in phase state machine; memory corruption; incorrect configuration; MCU hang during phase transition | Critical | Low | **Critical** | Hardware conflict matrix in independent Safety MCU; hardware inhibit GPIO; cannot be overridden by Application MCU |
| **H2** | Stuck-at-green (phase does not advance; green presented indefinitely) | Phase timer software bug; FreeRTOS task hang; queue deadlock preventing phase advance | Major | Low | High | Phase duration watchdog timer in Safety MCU; if no phase advance command received within maximum green + grace period, Safety MCU commands all-red |
| **H3** | Dark signal head (no aspects energised, no blackout commanded) | BTS7030 driver fault; cable open-circuit; LSO-100 CAN bus loss | Major | Medium | High | Per-channel current monitoring in LSO-100; fault detection < 500 ms; CAN heartbeat loss detection; fault log + inhibit |
| **H4** | Loss of control unit function (LCU-100 crash, watchdog reset, power loss) | Application MCU hang; firmware crash; power supply failure | Critical | Low | **Critical** | All-red held by LSO-100 charge pump for minimum 30 s on loss of CAN heartbeat; Safety MCU detects SPI loss within 30 ms; inhibit asserted |
| **H5** | Incorrect phase sequence (insufficient or missing intergreen period) | Software intergreen timer bug; incorrect configuration; Safety MCU bypassed | Critical | Low | **Critical** | Intergreen timer enforced in Safety MCU, not Application MCU; Safety MCU rejects phase requests that violate intergreen; Application MCU cannot override |
| **H6** | Battery brownout during operation (sudden undefined state) | Battery depletion; charging system failure; cold temperature | Major | Medium | High | Battery voltage monitoring (`PowerMonitorTask`); controlled shutdown sequence: all-red for minimum 30 s before power removal |
| **H7** | Incorrect phase configuration (wrong approach mapped to wrong output) | Commissioning error; configuration file corruption | Major | Low | High | Configuration CRC check on load; commissioning audit log; phase configuration requires PIN authentication and confirmation |
| **H8** | Signal head aspect mis-identification (green presented on wrong head) | Cable swap at installation; incorrect LSO-100 node ID assignment | Major | Low | High | Node ID assignment and aspect mapping are verified during commissioning self-test; self-test energises each channel individually and requires operator visual confirmation |
| **H9** | Loss of radio sync causing phase desynchronisation between controllers | RF interference; radio module failure (Phase 2) | Major | Medium | High | Phase 2 safety analysis required; currently out of scope |
| **H10** | Unintended manual override activation | Unauthorised access to commissioning interface; keypad error | Minor | Low | Low | Override requires PIN; override state is visually indicated on status LED; override times out automatically after 60 s without activity |

---

## 3. Safety Architecture

### 3.1 Architectural Overview

The LCU-100 safety architecture is based on two independent processors with separated responsibilities. Neither processor alone can cause hazard H1 (conflicting greens); both must simultaneously fail in specific, improbable ways.

```
┌──────────────────────────────────────────────────────────────────┐
│                    APPLICATION MCU (STM32H743)                   │
│                                                                  │
│   Traffic logic, phase planning, CAN comms, telemetry, GNSS     │
│                                                                  │
│   Requests phase changes → sends CMD_PHASE_REQUEST over SPI      │
│   Cannot directly energise any output                            │
└─────────────────────────┬────────────────────────────────────────┘
                          │ SPI (10 ms cycle)
                          │ Bidirectional, CRC-protected
                          ▼
┌──────────────────────────────────────────────────────────────────┐
│                    SAFETY MCU (STM32G071)                        │
│                                                                  │
│   Validates phase requests against conflict matrix               │
│   Enforces intergreen timers                                     │
│   Controls hardware inhibit GPIO                                 │
│   Independent IWDG (own RC oscillator)                           │
└─────────────────────────┬────────────────────────────────────────┘
                          │ Hardware inhibit GPIO (active-low, open-drain)
                          │ Pulled LOW at hardware reset — safe by default
                          ▼
┌──────────────────────────────────────────────────────────────────┐
│               HCPL-314J Opto-isolator Array                      │
│                                                                  │
│   Galvanic isolation between Safety MCU and output drive stage   │
│   Propagation delay < 1 μs                                       │
└─────────────────────────┬────────────────────────────────────────┘
                          │ Isolated inhibit signal (AND gate input)
                          ▼
┌──────────────────────────────────────────────────────────────────┐
│              AND Gate + BTS7030 INH Pin (per channel)            │
│                                                                  │
│   Channel enable = (LSO-100 output command) AND (inhibit HIGH)   │
│   If inhibit LOW → all channels disabled regardless of command   │
└──────────────────────────────────────────────────────────────────┘
```

### 3.2 Safety MCU Design Requirements

The Safety MCU (STM32G071) is designed to the following requirements:

| Requirement | Implementation |
|---|---|
| Independence from Application MCU power supply | Dedicated LDO regulator (AP2204K-3.3, separate input rail) |
| Independence from Application MCU clock | Internal RC oscillator (HSI, 64 MHz); no external crystal |
| Independence from Application MCU software | Separate firmware build; no shared source files; separate toolchain invocation |
| Hardware inhibit default state | Active-low, open-drain GPIO; 10 kΩ pull-down to GND; inhibit is the unpowered default |
| Conflict matrix storage | Stored in Safety MCU Flash; verified at startup using CRC32; not configurable remotely |
| Intergreen timer | Hardware timer (TIM1) in Safety MCU; not derived from Application MCU input |
| Watchdog | STM32G071 IWDG; 50 ms window; LSI RC oscillator; kicked in superloop |
| SPI slave validation | Frame validation (SOF, EOF, CRC8); 3 invalid frames → inhibit asserted |
| Phase request validation | Every request checked against conflict matrix before permissive granted |

### 3.3 LSO-100 Output Driver Stage

The LSO-100 (Lumina Signal Output module) contains the BTS7030 intelligent power switch ICs that drive the lamp filament or LED loads of each signal aspect. Each channel provides:

- **Current monitoring:** The BTS7030 IS pin provides a proportional current sense output. If the sense current indicates a channel is energised but drawing less than the expected minimum load current (indicating an open-circuit lamp or disconnected cable), an `OPEN_CIRCUIT_FAULT` is logged within 500 ms.
- **Overcurrent protection:** The BTS7030 provides hardware current limiting and thermal shutdown. An overcurrent or short-circuit condition is detected and reported via the diagnostic interface.
- **Inhibit input:** The BTS7030 INH pin is driven by the AND gate output. When the Safety MCU inhibit line is low, the AND gate output is low regardless of the LSO-100 logic, and all channels on that LSO-100 are disabled.

### 3.4 Independence Assessment

The following independence properties are claimed for the safety architecture:

| Independence Property | Evidence |
|---|---|
| Safety MCU power is independent of Application MCU | Separate LDO on separate PCB rail; Application MCU brownout does not affect Safety MCU supply |
| Safety MCU clock is independent of Application MCU | Safety MCU uses internal RC oscillator; external crystal failure on Application MCU has no effect |
| Safety MCU software is independent of Application MCU | No shared source files; no shared libraries; separate build |
| Hardware inhibit is independent of software state | Open-drain GPIO; hardware pull-down; cannot be set high by software crash or memory corruption |
| Inhibit path is independent of CAN bus | CAN bus health is not in the hardware inhibit logic path; CAN failure alone does not cause a hazardous state |

---

## 4. Conflict Matrix Definition

The conflict matrix defines which pairs of signal output channels must never be simultaneously energised. The Safety MCU evaluates every phase request against this matrix before granting a permissive.

The matrix below covers a standard 4-approach temporary signal layout (Approaches A, B, C, D) with a pedestrian stage. Amber and red aspects have no conflicts and are omitted for brevity.

### 4.1 Conflict Matrix (Green Aspects Only)

| | A Green | B Green | C Green | D Green | Ped Green |
|---|:---:|:---:|:---:|:---:|:---:|
| **A Green** | — | **CONFLICT** | **CONFLICT** | **CONFLICT** | **CONFLICT** |
| **B Green** | **CONFLICT** | — | **CONFLICT** | **CONFLICT** | **CONFLICT** |
| **C Green** | **CONFLICT** | **CONFLICT** | — | **CONFLICT** | **CONFLICT** |
| **D Green** | **CONFLICT** | **CONFLICT** | **CONFLICT** | — | **CONFLICT** |
| **Ped Green** | **CONFLICT** | **CONFLICT** | **CONFLICT** | **CONFLICT** | — |

**Key:**
- **CONFLICT**: These two outputs must never be simultaneously energised. The Safety MCU will reject any phase request that would result in this combination.
- —: Same output; not applicable.
- (Blank/omitted): No conflict (e.g., A Red and B Green may coexist).

### 4.2 Non-Conflicting Combinations

The following output combinations are explicitly permitted:

- Any number of red aspects simultaneously (all-red is the normal inter-phase state).
- Any amber aspect simultaneously with any red or other amber aspect (e.g., A Amber + B Red is permitted).
- A single green with any combination of ambers and reds on other approaches.
- All outputs inhibited (BLACKOUT state).

### 4.3 Conflict Matrix Verification

The conflict matrix is stored as a 20×20 boolean table in the Safety MCU Flash. At startup, the Safety MCU computes a CRC32 over the matrix and compares it against a hardcoded expected value. A mismatch causes the Safety MCU to assert the inhibit line and report `STAT_FAULT` in SPI status frames. The matrix cannot be modified remotely; changes require a Safety MCU firmware update, which requires physical access to the service UART.

The total number of output states for a 4-approach + pedestrian system is:

- 5 green outputs × 2 states (on/off) = 32 combinations
- Less states with more than one green = 32 − 16 = 16 single-green states + 1 all-off state = 17 valid permissive states.
- All 17 valid states are verified against the conflict matrix at startup and logged as passing in the boot event log.

---

## 5. Intergreen Timing (UK TOPAS Requirement)

### 5.1 Regulatory Background

TOPAS 2540A (Traffic Signal Equipment: Portable Traffic Signals) specifies minimum intergreen periods for temporary signals used on UK highways. The intergreen period is the time between the end of one phase's green aspect and the beginning of the next competing phase's green aspect. It comprises:

1. **Amber clearance period:** The outgoing phase displays amber before extinguishing.
2. **All-red period:** All approaches display red simultaneously, providing a conflict-free clearance time.

### 5.2 Minimum Intergreen Values

| Period | Minimum Duration | Enforcement Location |
|---|---|---|
| Amber clearance | 3 seconds | Safety MCU intergreen timer |
| All-red clearance | 2 seconds | Safety MCU intergreen timer |
| Total intergreen (A→B or any competing pair) | 5 seconds | Sum of above |

These minima apply between any two competing phases (i.e., any two phases that would result in conflicting greens if run consecutively without intergreen).

**Example intergreen sequence (Phase A → Phase B):**

```
Time: 0        3        5
      |        |        |
A:   [GREEN]──[AMBER]──[RED]─────────────────────────────────────
B:   [RED]──────────────────[RED]──[GREEN]
                            |      |
                         All-red   Green permitted
                         (2 s)     only after 5 s total
```

### 5.3 Intergreen Timer Implementation

The intergreen timer is implemented entirely within the Safety MCU:

1. When the Application MCU sends `CMD_PHASE_REQUEST` for Phase B while Phase A is active:
   - If Phase A is currently displaying green, the Safety MCU does **not** immediately grant the permissive for Phase B green.
   - The Safety MCU issues an output command to the LSO-100 (via CAN FD) to display Phase A amber.
   - TIM1 is started with a 3-second amber period.
2. On TIM1 expiry (amber period complete):
   - Safety MCU commands all-red.
   - TIM1 is restarted with a 2-second all-red period.
3. On second TIM1 expiry (all-red period complete):
   - Safety MCU evaluates Phase B request against conflict matrix.
   - If no conflict, permissive for Phase B green is granted; `STAT_PERMISSIVE` set in SPI status.
4. The Application MCU is responsible for issuing the Phase B green command; it does not automatically energise on permissive grant.

The Application MCU **cannot** abbreviate or skip the intergreen timer. The Safety MCU ignores any `CMD_PHASE_REQUEST` received while the intergreen timer is running; `STAT_PHASE_REJECTED` is returned in the status frame.

### 5.4 Timer Independence

The intergreen timer uses TIM1 on the STM32G071, clocked by the internal RC oscillator. It does not use any Application MCU-provided timing signal. The 3-second and 2-second intervals are accurate to ±2% over the operating temperature range (−20°C to +60°C), consistent with the STM32G071 HSI RC oscillator specification.

---

## 6. Fail-Safe States

### 6.1 Fail-Safe Design Principle

All failure modes of the LCU-100 are designed to drive the system towards a defined, observable, safe state. The priority ordering of states (most to least safe) is:

1. **All outputs inhibited (hardware inhibit active):** No aspects energised; drivers see a dark junction. This is the safest state for controller failures. Acceptable for short durations; requires traffic management personnel or police control for extended outages.
2. **All-red:** All approaches display red; no traffic authorised. Safe and self-managing for temporary durations.
3. **Single phase active with correct intergreen:** Normal operation.

### 6.2 Fail-Safe State Table

| Trigger Condition | Detection Mechanism | Response | Safe State Achieved |
|---|---|---|---|
| Power-on / hardware reset | Hardware default | All outputs inhibited (pull-down on inhibit line) | Inhibit active |
| Application MCU watchdog expiry | IWDG fires → MCU reset; Safety MCU detects SPI heartbeat loss within 30 ms | Safety MCU asserts inhibit; LSO-100 charge pump holds all-red for ≥ 30 s | Inhibit, then all-red |
| Application MCU SPI frame invalid (×3 consecutive) | Safety MCU frame validator | Safety MCU asserts inhibit immediately | Inhibit active |
| Safety MCU watchdog expiry (IWDG) | IWDG fires → STM32G071 reset | Inhibit line not driven during reset; pull-down asserts inhibit | Inhibit active |
| CAN FD bus failure (LSO heartbeat loss) | Safety MCU CAN monitor; Application MCU CAN Rx monitor | Application MCU: Traffic Engine → FAULT_LOCKOUT; Safety MCU: all-red maintained on last-known state for ≤ 10 s, then inhibit | All-red, then inhibit |
| Battery voltage critical | `PowerMonitorTask` ADC monitoring | Controlled all-red command; all-red held for minimum 30 s; then controlled shutdown | All-red (30 s minimum) |
| Conflict matrix violation (spurious energise attempt) | Safety MCU conflict matrix check | Phase request rejected; `STAT_CONFLICT_DETECTED` set; fault logged | No change (previous safe state maintained) |
| Intergreen violation attempt | Safety MCU intergreen timer | Phase request rejected; `STAT_PHASE_REJECTED` set; fault logged | No change (intergreen continues) |
| Stack overflow in any task | FreeRTOS stack overflow hook | FAULT_STACK_OVERFLOW logged; IWDG allowed to expire | Inhibit (after ≤ 50 ms IWDG) |
| FRAM read/write failure | `FaultLogTask` error return | FAULT_FRAM_ERROR logged to RAM; operation continues; telemetry warning | No safety impact; logging degraded |

### 6.3 All-Red Hold by LSO-100 Charge Pump

Each LSO-100 module contains a supercapacitor charge pump that can maintain the all-red drive for up to **60 seconds** without receiving a CAN frame. If the LCU-100 stops transmitting CAN frames (due to reset, power loss, or software crash), the LSO-100 modules continue to hold all-red for this period. After 60 seconds without a CAN heartbeat, the LSO-100 transitions to the inhibited state (all outputs off).

This provides a controlled transition for drivers: they observe a continuous all-red (stop) for 60 seconds before signals go dark, rather than an abrupt dark state.

### 6.4 Battery Low vs Battery Critical Thresholds

| State | Voltage Threshold | Action |
|---|---|---|
| Normal operation | ≥ 12.0 V | No action |
| Battery low warning | < 12.0 V | `SYSEVT_BATTERY_LOW` set; telemetry alert; operator notification |
| Battery critical | < 11.0 V | Controlled shutdown sequence initiated: all-red held for minimum 30 s, then controlled power-off |
| Emergency cutoff | < 10.5 V | Hardware UVP (undervoltage protection) on PSU; all outputs lose power |

The 30-second all-red hold before controlled shutdown is enforced by `TrafficEngineTask` in conjunction with `PowerMonitorTask`. The Safety MCU additionally holds all-red via the LSO-100 charge pump if LCU power is lost before the 30-second period expires.

---

## 7. Items Outside This Safety Concept

The following items are explicitly **not** covered by this preliminary safety concept. They require separate safety analysis before the LCU-100 can be used in configurations where these features are active.

| Item | Status | Required Action Before Use |
|---|---|---|
| **RF-controlled synchronisation (Phase 2)** | Not designed; Phase 2 roadmap | Separate safety analysis required; failure modes of RF sync (lost sync, rogue transmission, replay attack) must be assessed before any synchronised multi-controller deployment |
| **Pedestrian protection (LPI-100)** | Phase 2 | Pedestrian conflict matrix and demand-based control must be assessed under IEC 61508 Part 2; distinct from vehicle phase conflicts |
| **Formal SIL claim and probability calculations** | Not performed | Requires systematic IEC 61508 Part 2 analysis including FMEA, common cause analysis, and diagnostic coverage assessment; PFH/PFD values must be derived and compared to target SIL |
| **TOPAS 2540A approval testing** | Not commenced | Requires submission to a UKAS-accredited test body; includes environmental testing, EMC, electrical safety, and functional testing per TOPAS 2540A annex |
| **Highway authority type approval** | Not commenced | Requires TOPAS approval as a prerequisite; then submission to relevant highway authority or Highways England for type approval |
| **Safety case for multi-unit networks** | Not applicable (single-unit scope) | If more than one LCU-100 is used in a co-ordinated scheme (e.g., convoy working), a scheme-level safety case is required |
| **Cybersecurity analysis** | Preliminary only | LTE OTA update path and MQTT broker authentication require a threat model and penetration test before deployment on public networks |

---

## Document Control

| Rev | Date | Author | Change Description |
|---|---|---|---|
| 0.1 | 2026-03-10 | Safety Engineering | Initial draft for internal review |
| 0.2 | 2026-05-02 | Safety Engineering | Updated hazard table; added conflict matrix section; added fail-safe state table |
| 0.3 | 2026-06-18 | Safety Engineering | Revised to reflect dual-MCU architecture; updated intergreen implementation; added Section 7 exclusions list |

---

*End of Document LCU-100-SC-001 Rev 0.3*

*This is a PRELIMINARY document. It does not constitute a safety case, SIL claim, or evidence of compliance with any standard. See the important notice on page 1.*

*This document is Lumina Engineering confidential. Do not distribute outside Lumina Engineering without written authorisation.*
