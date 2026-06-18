# Lumina Controller Platform — PCB Assembly and Prototype Build Guidelines

**Document Number:** LUM-MFG-001  
**Revision:** A  
**Status:** Prototype / First Article  
**Applies To:** LCU-100, LSO-100, LPB-100, LPI-100  
**Prepared By:** Lumina Engineering  
**Date:** 2026-06-18  

---

## 1. Document Scope and Applicability

### 1.1 Purpose

This document defines the PCB assembly, inspection, flashing, and serialisation requirements for the Lumina temporary traffic signal controller platform, comprising the following boards:

| Board | Description |
|-------|-------------|
| LCU-100 | Controller Unit — STM32H743-based main controller with CAN, RS-485, LTE/GNSS |
| LSO-100 | Signal Output board — 8-channel PROFET-switched lamp driver |
| LPB-100 | Power and Battery board — BQ76952 BMS, BQ25798 MPPT charger, 48 V Li-ion management |
| LPI-100 | Pilot Interface — keypad, display, status LEDs, operator controls |

### 1.2 Batch Scope

These guidelines apply to the first prototype batch of **5 to 10 complete sets** (one set = one each of LCU-100, LSO-100, LPB-100, LPI-100). Subsequent pre-production and production builds will be governed by the released manufacturing procedure document (LUM-MFG-002, TBD).

### 1.3 Quality Standards

- **General assembly:** IPC-A-610 Class 2 as the minimum acceptance criterion.
- **Safety-critical solder joints:** IPC-A-610 **Class 3** mandatory for the following components:
  - STM32H743 (LQFP-144) — main controller
  - BTS7030-2EPA PROFET — high-current switching, safety interlock path
  - BQ76952 (TQFP-64) — battery management, over-current and over-voltage protection
  - Any component in the output inhibit or watchdog circuit
- **Rework:** IPC-7711/7721 procedures shall be followed for all rework operations. Rework beyond two cycles on a Class 3 joint requires engineering approval and re-inspection.
- **PCB design reference:** IPC-2221 generic standard for PCB design; deviations are noted in the individual board design files.

### 1.4 Referenced Documents

| Document | Title |
|----------|-------|
| IPC-A-610 Rev G | Acceptability of Electronic Assemblies |
| IPC-7711/7721 Rev C | Rework, Modification and Repair of Electronic Assemblies |
| IPC-2221B | Generic Standard on Printed Board Design |
| IPC-7525B | Stencil Design Guidelines |
| J-STD-001 Rev H | Requirements for Soldering Electrical and Electronic Assemblies |
| J-STD-020 Rev E | Moisture/Reflow Sensitivity Classification for Nonhermetic SMD Packages |
| LUM-SCH-100-A | LCU-100 Schematic Package |
| LUM-SCH-101-A | LSO-100 Schematic Package |
| LUM-SCH-102-A | LPB-100 Schematic Package |
| LUM-SCH-103-A | LPI-100 Schematic Package |
| LUM-BOM-100-A | Master BOM, all boards |

---

## 2. Component Procurement and Incoming Inspection

### 2.1 Approved Suppliers

For the prototype batch, **all active and passive components must be sourced exclusively from the following authorised distributors:**

- Mouser Electronics
- Farnell / Element14
- RS Components

No grey-market, broker, or unverified marketplace sourcing is permitted for this batch. Components purchased outside this list require written authorisation from the lead engineer and additional incoming inspection as described in Section 2.3. This restriction will be reviewed for pre-production to allow approved alternatives (e.g., Digi-Key, Arrow, Avnet).

### 2.2 Date Code Verification

All active ICs (microcontrollers, MOSFETs, BMS ICs, LDOs, transceivers, power management ICs) shall have date codes verified on receipt:

- Maximum allowable date code age: **24 months** from date of manufacture to date of first use on PCB.
- Date codes shall be recorded in the component receiving log.
- Components with absent, illegible, or inconsistent date codes shall be quarantined and shall not be used without engineering disposition.

### 2.3 Moisture Sensitivity Level (MSL) Requirements

The following key components are rated MSL 3 per J-STD-020 and require controlled floor life management:

| Component | Package | MSL | Floor Life (30°C / 60% RH) |
|-----------|---------|-----|---------------------------|
| STM32H743VIT6 | LQFP-144 | 3 | 168 hours |
| BTS7030-2EPA | PG-DSO-14 | 3 | 168 hours |
| Quectel EC21-AU | LCC/LGA | 3 | 168 hours |
| BQ76952PFBR | TQFP-64 | 3 | 168 hours |
| BQ25798RQMR | VQFN-40 | 3 | 168 hours |

**Floor life tracking:** Each reel or tray shall have a floor life label applied upon opening. The label shall record: date/time opened, operator, and calculated discard or bake-by date/time.

**Bake procedure if floor life exceeded:**
- Bake at **40°C ± 2°C** for **192 hours (8 days)** in a desiccating oven.
- Alternative: 60°C for 72 hours is permissible for components that have not been mounted to a PCB.
- After baking, floor life resets to the full rated period.
- Components must be soldered within the floor life window after baking. A second bake cycle is permissible; a third cycle requires engineering review.
- Record all bake cycles in the component traveller log.

MSL 1 components (most passives, standard SOT-23 discretes) may be stored on open shelving in the assembly area with no floor life restriction.

### 2.4 Incoming Inspection Procedures

**All components:**
- Verify against BOM for part number, manufacturer, and package.
- Check for physical damage, bent leads, crushed reels.
- Verify moisture barrier bags are sealed and humidity indicator card reads ≤10% (blue) for MSL 2 and above.

**BGA and QFN components (incoming X-ray):**
- BQ25798RQMR (VQFN-40): perform X-ray to verify no voids in thermal pad and no ball damage from shipping.
- Any BGA-packaged component (if added to BOM in future revisions): mandatory X-ray.
- X-ray acceptance: solder ball voiding < 25% per IPC-7095.

**QFP components (lead inspection):**
- STM32H743VIT6 (LQFP-144), BQ76952PFBR (TQFP-64): inspect all four sides under 10× magnification or optical comparator.
- Acceptance criterion: lead coplanarity ≤ 0.1 mm (per IPC-A-610).
- Any bent, twisted, or out-of-plane lead: reject to supplier. Do not attempt to straighten QFP leads.

---

## 3. PCB Handling and ESD Precautions

### 3.1 ESD Control

- All assembly operations shall be performed in an **ESD-controlled area** meeting IPC-A-610 and IEC 61340-5-1 requirements.
- Personnel shall wear a calibrated **wrist strap** grounded to the ESD mat at all times when handling bare boards or populated assemblies.
- Wrist strap continuity shall be tested at the start of each shift and logged. Out-of-tolerance straps shall be replaced before work continues.
- Anti-static mat covers all work surfaces. Benches are not to have plastic food/drink containers, ordinary plastic bags, or non-ESD foam on the surface while boards are present.
- Use ESD-safe tweezers and placement tools only.

### 3.2 PCB Storage

- Unpopulated PCBs: store in **poly bags with silica gel desiccant**, sealed. Store in a controlled environment (15–30°C, <70% RH).
- Do not stack bare PCBs without interleaving foam or separator cards.
- Boards with ENIG or OSP finish are particularly susceptible to oxidation if stored unbagged; use within 12 months of manufacture date.

### 3.3 PCB Surface Finish

| Prototype Stage | Permitted Finish |
|----------------|-----------------|
| Initial prototype (batch 1) | HASL lead-free (HASL-LF) is acceptable for coarse-pitch components |
| Preferred / connector-bearing boards | ENIG (Electroless Nickel Immersion Gold) — required for fine-pitch QFP, QFN, and spring-contact connectors |
| M.2 socket pads | ENIG mandatory |

Note: HASL-LF boards may exhibit slight pad height variation that can affect QFN solder paste volume. If HASL-LF boards are used for LPB-100 (contains BQ25798 VQFN-40 and BQ76952 TQFP-64), exercise additional care during stencil print and inspect paste volume thoroughly before placement.

### 3.4 PCB Visual Acceptance (Incoming)

Before any assembly begins, each bare PCB shall be inspected:

- No delamination, bubbling, or discolouration of laminate.
- No measling or crazing visible through laminate (use backlighting).
- All drill holes present and clean (no residue or torn fibres).
- Silkscreen readable and correctly registered.
- Layer count verification: remove one PCB per panel from the batch for **cross-section sample inspection**. Verify the actual layer count and stack-up against the fabrication drawing. This is mandatory for the first batch from each PCB supplier.
- ENIG boards: verify gold colour uniformity (no black pad areas). Reject boards with localised dull or dark ENIG patches.

---

## 4. Solder Paste and Stencil

### 4.1 Solder Paste Specification

- **Alloy:** SAC305 (Sn96.5 / Ag3.0 / Cu0.5) — RoHS compliant, lead-free.
- **Flux type:** No-clean, ROL0 classification (low residue, no halides). Preferred brands: Indium Corporation NC-SMQ92J, Kester R256, or equivalent to IPC J-STD-004 ROL0.
- **Particle size:**
  - Type 4 (25–38 µm): standard for 0603 and larger components.
  - Type 4.5 (20–38 µm): required for 0402 and smaller passives, and for fine-pitch ICs. Use Type 4.5 across all boards if a single paste is preferred.
  - Type 5 (15–25 µm): not required for current BOM but may be beneficial for BQ25798 thermal pad apertures.
- **Storage:** 2–10°C refrigerated. Warm to room temperature for minimum **4 hours** before opening. Do not accelerate warming. Record jar open date; discard after 6 months open or per manufacturer shelf life, whichever is sooner.
- **Working pot life:** paste on stencil should not be worked for more than 4 hours without replenishment under normal SMT shop conditions (20–25°C, 40–60% RH).

### 4.2 Stencil Specification

- **Material:** 120 µm (0.12 mm) laser-cut **stainless steel** — electropolished aperture walls preferred for Type 4/4.5 paste release.
- **Aperture area ratio:** minimum 0.66 required for acceptable paste release. This constrains the minimum aperture size for a given stencil thickness. Verify against IPC-7525B.

| Component / Area | Stencil Thickness | Aperture Treatment |
|------------------|------------------|--------------------|
| General (0603 and larger) | 120 µm | 1:1 pad, no reduction |
| 0402 passives | 120 µm | 10% aperture reduction on all sides |
| 0201 passives (if used) | Step-down to 100 µm | 20% reduction; require stencil step-down in that region |
| QFN thermal pads (BQ25798) | Step-down to 100 µm | Segmented aperture (4×4 grid), cover 50–60% of thermal pad area |
| BQ76952 TQFP-64 (0.5 mm pitch) | 120 µm | Full pad exposure, no reduction (0.5 mm pitch adequately manageable with Type 4.5) |
| STM32H743 LQFP-144 (0.4 mm pitch) | 120 µm | 1:1 aperture — Type 4 or 4.5 paste adequate for 0.4 mm pitch at 120 µm |
| M.2 socket castellations | 120 µm | Per M.2 land pattern, no modification |

- **Step-down regions** for QFN thermal pads and 0201 areas shall be electroformed step-down areas, not a separate stencil, to maintain registration accuracy.
- The stencil drawing number shall match the PCB revision. Stencils from a prior PCB revision must not be used without engineering review.

### 4.3 Screen Printing Process

- Use automatic or semi-automatic screen printer with board support and vision registration.
- Snap-off distance: 0 mm (on-contact printing) or ≤ 0.5 mm snap-off for supported boards.
- Print speed: 25–50 mm/s for fine-pitch areas.
- Squeegee pressure: 0.1–0.15 N/mm (7–10 kgf for a 70 mm squeegee) — set empirically to achieve full aperture fill without smearing.
- Squeegee angle: 60° to 65° from board surface.
- Print direction: bi-directional for throughput. Validate that bi-directional print does not cause bridging on LQFP-144 pads; if bridging observed, switch to single-direction.
- Clean stencil underside every 5 prints (auto-wipe cycle) or every 10 prints for less critical builds. Use IPA-dampened lint-free wipe, followed by dry wipe.

### 4.4 Solder Paste Inspection (SPI)

- **100% automated paste inspection (SPI/AOI paste)** is required before any component placement.
- SPI system shall measure paste volume, height, area, and offset for all pads.
- Acceptance limits:
  - Volume: 50%–150% of nominal
  - Height: ≥ 60% of stencil thickness (72 µm for 120 µm stencil)
  - Offset: ≤ 25% of pad width
- Any board failing SPI shall be cleaned (IPA wash, ultrasonic if required) and reprinted. Do not place components on a board with out-of-tolerance paste deposits.
- SPI data shall be logged per board serial number for process capability tracking.

---

## 5. Component Placement Sequence — LCU-100 Example

The following sequence applies to the LCU-100 board and is representative of the general approach for all boards. Refer to board-specific notes in Sections 11–13 for LSO-100, LPB-100, and LPI-100.

| Step | Operation | Notes |
|------|-----------|-------|
| 1 | Bare board visual inspection | Per Section 3.4; sign off before proceeding |
| 2 | PCB moisture bake (if required) | Only if PCB has been stored >30 days unbagged in humid conditions |
| 3 | Solder paste print | Per Section 4 |
| 4 | SPI (automated paste inspection) | 100% — no exceptions |
| 5 | Place 0402 passives (R, C) | Pick and place; verify feeder loading against BOM |
| 6 | Place SOT-23/SOD discretes (transistors, diodes, small regulators) | Polarity-sensitive — vision system must verify D1, D2 etc. |
| 7 | Place large QFP ICs: STM32H743VIT6 (LQFP-144), STM32G071 (LQFP-64 or similar) | Vision alignment system mandatory; use fiducials and local pad recognition |
| 8 | Place QFN/DFN ICs: TPS7A4700 (HTSSOP-16-EP), AP2114 (SOT-223), BQ25798 if on this board | Centroid and rotation accuracy critical; thermal pad must be centred |
| 9 | Place SOP/SOIC ICs: TCAN1042 (SOIC-8), MAX3485 (SOIC-8), FM25V10 FRAM (SOP-8), RV3028 RTC (SOT-23-8) | Standard placement; check orientation via pin 1 chamfer |
| 10 | Place connectors: USB-C (J5), SWD header (J8), any SMD connector bodies | Last, as they may overhang or interfere with vacuum nozzles on adjacent parts |
| 11 | Manual placement inspection | Verify all large ICs are seated and not tilted; check any parts rejected by vision system |
| 12 | Reflow oven | Per Section 6 profile |
| 13 | Post-reflow AOI | Per Section 7 |
| 14 | Post-reflow X-ray | Per Section 7 — QFN thermal pads and QFP lead inspection |
| 15 | Through-hole and connector assembly | Per Section 8 |
| 16 | Functional test | Per test procedure LUM-TEST-100 |
| 17 | Conformal coat | Per Section 9 |
| 18 | Serialisation | Per Section 10 |

**MiniFit Jr. and M12 connectors** are through-hole or panel-mount and are assembled after reflow; see Section 8.

---

## 6. Reflow Profile — SAC305, ENIG Board

The following profile applies to SAC305 solder paste on ENIG-finished PCBs. HASL-LF boards have a slightly lower thermal mass at the surface and may reach peak temperature marginally faster; monitor with thermocouple and adjust conveyor speed if needed.

### 6.1 Profile Parameters

| Zone | Temperature Range | Ramp Rate | Duration |
|------|------------------|-----------|----------|
| Preheat | 25°C → 150°C | 1–2°C/s | 60–90 s |
| Soak (thermal equalisation) | 150°C → 180°C | 0.5°C/s max | 60–90 s |
| Ramp to reflow | 180°C → 217°C (liquidus) | 1–2°C/s | ~20 s |
| Reflow (above liquidus) | 217°C → peak (~240–245°C) | 1–2°C/s | 45–60 s above liquidus |
| Peak | 240–245°C nominal; 245°C max at component body | — | <10 s at peak |
| Cooling | 245°C → 100°C | ≤ −2°C/s (do not exceed) | — |

**Peak temperature notes:**
- General components: 245°C maximum at component body.
- M.2 socket (if present): 260°C maximum per JEDEC standard; local thermocouple verification required.
- Quectel EC21 module: verify manufacturer's reflow specification — typically 245°C peak.
- Anderson SB connectors are not to be reflowed — hand assembly only (Section 8).

### 6.2 Profile Validation

- Validate the profile using **K-type thermocouples** attached with Kapton tape and heat-stable adhesive (e.g., Omegabond 200) to:
  - STM32H743 package body (top surface)
  - Underside of BQ76952 TQFP-64 (or as close as possible via adjacent via)
  - Thermal pad of BQ25798 VQFN-40 (via thermocouple soldered to a test pad adjacent to thermal pad)
  - A corner of the board (typically lowest thermal mass — first to reach peak)
  - Centre of the board (typically highest thermal mass — last to reach peak)
- Profile validation must be repeated:
  - For the first board of each new PCB revision.
  - Whenever oven settings or conveyor speed are changed.
  - At the start of a new batch if more than 30 days have elapsed since last validation.
- Record thermocouple profiles and store with the board traveller documentation.

### 6.3 Board Support and Handling

- Boards larger than 100 × 100 mm shall use edge rails plus centre support pins in the reflow oven to prevent board sag.
- Do not allow boards to touch each other in the oven. Minimum 10 mm board-to-board clearance on the conveyor.
- Handle boards immediately post-reflow with board handling gloves or tweezers — do not touch pads or component bodies with bare hands while still warm.
- Allow boards to cool to < 40°C before placing in carriers or stacking.

---

## 7. Post-Reflow Inspection

### 7.1 Automated Optical Inspection (AOI)

- **100% AOI** is required after reflow for all boards in the prototype batch.
- AOI program must be validated (golden board teach-in) before first production run.
- **Log all false calls** (defects flagged by AOI that are confirmed acceptable on re-inspection). High false-call rates indicate AOI program requires tuning.
- True defects found by AOI shall be logged, classified (solder joint, component placement, missing component, polarity), and reworked per IPC-7711/7721 before proceeding to X-ray.
- AOI shall inspect: solder joint fillet presence and shape, component presence, polarity markings (where visible), component skew and offset.

### 7.2 X-Ray Inspection

Mandatory X-ray for the following after reflow:

| Component | X-Ray Purpose | Acceptance Criterion |
|-----------|--------------|---------------------|
| STM32H743 LQFP-144 | Lead coplanarity / solder fillet uniformity | All leads soldered, no opens, coplanarity < 0.1 mm deviation visible in X-ray; no visible bridging |
| BQ76952 TQFP-64 | Full perimeter lead contact; thermal pad voiding if applicable | All four sides soldered, < 25% voiding on any thermal pad |
| BQ25798 VQFN-40 | Thermal pad contact, voiding, possible shorts under package | < 25% void in thermal pad per IPC-7093 criteria |
| TPS7A4700 HTSSOP-EP | Exposed pad contact | > 75% thermal pad coverage |

For the prototype batch, **all boards** shall be X-rayed. For production, sample rates per IPC-A-610 may be applied once process capability is established.

### 7.3 Visual Inspection — Key Areas

In addition to AOI, perform targeted manual visual inspection under 10× stereomicroscope on:

**BTS7030-2EPA (LSO-100):**
- All PG-DSO-14 leads: 0.65 mm pitch, inspect for bridging between adjacent pins.
- Confirm exposed pad on underside is soldered — visible as solder fillet at pad edges. If no fillet visible, X-ray to confirm thermal pad contact.

**M.2 Socket (if present):**
- Verify socket is flush to board — no visible rocking or misalignment.
- All castellations soldered; check both sides of connector.

**INA219 (SOT-23-8 / MSOP-8) — LSO-100:**
- Verify pin 1 orientation against silkscreen before and after reflow.
- Post-reflow: check all 8 pins soldered, no bridges between pin 1 (IN+) and pin 2 (IN−). The polarity of IN+ and IN− determines current direction readback — an inverted INA219 will report negative current in all conditions.

**BQ76952 (TQFP-64) — LPB-100:**
- Inspect all four sides under magnification. TQFP-64 at 0.5 mm pitch has 16 pins per side.
- Confirm no tombstoned 0402 or 0201 components in the immediate vicinity (strong thermal gradient from large IC package can tombstone nearby small passives).
- Verify all VCC bypass capacitors (10 µF + 100 nF per pin) are present and correctly positioned — their absence will cause BQ76952 to malfunction or be damaged.

**Connectors (USB-C, SWD header):**
- USB-C: verify all 24 pads contacted (Shell + signal pads). Check for solder under shield shell.
- SWD header (J8, J9): 10-pin 1.27 mm; verify all pins soldered and no bridging.

---

## 8. Through-Hole and Connector Assembly

### 8.1 MiniFit Jr. Connectors (Molex)

- Hand solder using temperature-controlled iron at **330–350°C**, 1.6 mm conical or chisel tip.
- Apply flux to pin before soldering.
- Solder fillet acceptance: IPC-A-610 **Class 2** — 75% through-board wicking or full barrel fill preferred. Avoid cold joints (dull, grainy appearance).
- Allow to cool fully before mating or applying mechanical load.
- Do not exceed 360°C or apply iron for more than 5 seconds per pin to avoid PCB laminate damage.

### 8.2 M12 Panel Connectors (IP67, 5-pin, A-coded CAN / D-coded Ethernet as applicable)

- Assemble connectors to PCB first by soldering the PCB-mount pins or cable pigtail as applicable.
- **Then** mount the PCB into the enclosure and tighten the M12 connector body to the panel from the outside.
- Tighten to manufacturer torque specification — typically **0.6–0.8 N·m** for M12 panel nut. Do not over-torque (risk of cracking plastic bodies or pulling pads if torque applied before PCB mounting screws are fixed).
- Verify M12 connector thread engagement is ≥ 5 turns for mechanical security.

### 8.3 SMA RF Connectors (GNSS, LTE)

- For PCB-edge SMA connectors: solder **centre pin first** using 50 W iron at 350°C; dwell time ≤ 3 seconds.
- Solder ground tabs second — tabs act as heat sinks and require brief iron contact to avoid cold joints.
- After soldering, verify connector alignment is perpendicular to board edge within ±2°.
- If RF test equipment (VNA or cable/antenna tester) is available, measure VSWR at the SMA port; acceptance criterion VSWR < 1.5:1 at the operating frequency band (700–2100 MHz for EC21 LTE; 1575 MHz for GNSS).
- If VNA is not available, continuity-check centre pin to RF trace and ground tabs to ground plane; verify no short between centre pin and shell.

### 8.4 Anderson SB50 / SB120 Power Connectors (LPB-100)

- Anderson SB connectors are **crimp-only** — do not solder the cable into Anderson contacts.
- Crimp tool: use Anderson-approved crimp tool for SB series contacts. Verify crimp tool calibration is current (annual calibration recommended).
- Use the correct contact size for the cable gauge (SB50 contacts rated for 16–6 AWG; SB120 contacts rated for 4–2/0 AWG — match to cable in BOM).
- After crimping, pull-test each contact at 50 N (for SB50) or 100 N (for SB120) — contact must not pull out of crimp.
- Insert contacts into housing by pushing until the retaining tab clicks (audible and tactile). Tug lightly to confirm retention.
- Apply heat-shrink over contact/cable entry point for mechanical strain relief.

### 8.5 Superseal 1.5 Connectors (LSO-100 Signal Outputs)

- Crimp tool: TE Connectivity **91525-1** or equivalent for 1.5 mm Superseal terminals.
- Verify crimp tool calibration before each build session.
- Conductor insulation must be stripped cleanly to the correct length (refer to TE application specification for 1.5 mm contacts — typically 3.0 mm strip length).
- Insert contacts until audible click; verify retention.
- Apply housing locking clip after assembly; verify clip is seated.

### 8.6 Screw Terminals (PCB-Mount)

- Tighten PCB-mount screw terminals for power connections to **0.5–0.6 N·m**.
- Do not exceed 0.6 N·m on M2.5 or M3 screws in plastic terminal bodies — risk of thread stripping.
- Use a calibrated torque screwdriver; do not estimate by feel alone.
- After tightening, tug each wire to confirm it is gripped — no pullout under 20 N.

---

## 9. Conformal Coating

### 9.1 Timing and Sequence

Conformal coating shall be applied **after successful functional test** and board serialisation (Section 10), and before final enclosure integration. Applying conformal coat before functional test prevents easy rework access and may mask early failures.

### 9.2 Material

- **Product:** Humiseal 1B31 acrylic conformal coat (or equivalent IPC-CC-830B Type AR compliant material).
- **Application method:** spray (aerosol for prototype batch; selective spray gun for production) or brush for touch-up and repair.
- **Dry film thickness:** 50–100 µm. Thinner coatings (< 25 µm) do not provide adequate environmental protection. Thicker coatings (> 150 µm) may crack under thermal cycling.
- Humiseal 1B31 thinner: use 521 thinner if thinning for spray; do not dilute more than 10% by volume.

### 9.3 Masking Requirements

Mask the following areas before coating. Use self-adhesive tape (Kapton or similar high-temperature masking tape), plug gauges, or conformal coat mask compounds:

| Location | Reason |
|----------|--------|
| J1–J8 connector bodies (mating faces) | Must remain uncoated for electrical connection |
| SWD header J8 / J9 (if fitted) | Test and service access |
| USB-C connector J5 (mating area) | Mechanical and electrical |
| SMA connectors (mating thread and centre pin) | RF connection and impedance integrity |
| Mounting holes and exposed PE pads | Earth bonding effectiveness |
| Any test pad array used for functional test | Service access |
| RTC battery holder contacts (if coin cell used) | Mechanical fit |

Remove masking after coating and inspect removal has not disturbed coating at mask edges.

### 9.4 Cure Schedule

| Cure Method | Conditions | Duration |
|-------------|-----------|---------|
| Accelerated (oven) | 50°C ± 5°C forced air | 30 minutes |
| Ambient cure | 20–25°C, >40% RH | 24 hours |

Ensure adequate ventilation (solvent fumes) during spray application and initial cure.

### 9.5 Inspection

- **UV lamp inspection:** Humiseal 1B31 is UV-fluorescent. Inspect all coated boards under UV lamp (365 nm) for:
  - Uniform coverage over intended areas.
  - Absence of coating on masked connector faces.
  - No dewetting, pinholes, or runs.
- Any voids or thin spots over safety-critical components (BTS7030, BQ76952, STM32H743) shall be touched up with brush coat and re-cured.
- Record UV inspection result (pass/fail with any rework notes) in the board traveller.

---

## 10. Board Serialisation

### 10.1 Serial Number Format

Each board shall be serialised before conformal coating using a **Datamatrix barcode label** (GS1 DataMatrix or similar; minimum module size 0.5 mm for machine readability; apply to a stable, flat area of the PCB silkscreen layer).

Serial number format:

```
[Board Type]-[HW Rev]-[Sequence]
Example: LCU-100-A-000001
         LSO-100-A-000001
         LPB-100-A-000001
         LPI-100-A-000001
```

Where:
- **Board Type:** LCU-100, LSO-100, LPB-100, LPI-100
- **HW Rev:** A (first prototype), incremented for each PCB layout revision
- **Sequence:** 6-digit zero-padded integer, sequential within board type and revision

### 10.2 FRAM Serial Storage

- At the test station, use the production test script to write the board serial number to the onboard **FM25V10 FRAM** at address **0xFFFF00** (last 6 bytes of FRAM address space, or as defined in firmware memory map document LUM-FW-MAP-001).
- The write shall be followed by a read-back verification. The test script must confirm the written bytes match the intended serial number before the board is marked as serialised.
- If FRAM write fails, investigate SPI communication (check U3 orientation, power supply, SPI clock polarity) before proceeding.

### 10.3 Production Database Registration

Each board serial number shall be entered into the production database (spreadsheet or MES as available) with:
- Board serial number
- PCB fabrication batch number and supplier
- Date of assembly
- Operator ID
- BOM revision
- Firmware version flashed (bootloader + application, with git commit hash or version tag)
- Test result (pass/fail; if fail, fault description and rework action taken)
- Conformal coat batch number

---

## 11. LSO-100 Specific Assembly Notes

### 11.1 BTS7030-2EPA PROFET — Thermal Pad Soldering

The BTS7030-2EPA PG-DSO-14 package has an **exposed thermal pad** on the underside of the package. This pad carries the drain current and must be properly soldered to the PCB thermal land for both electrical and thermal performance:

- A minimum of **4 thermal vias** (0.3 mm drill, filled and capped, or plugged) shall be present directly under the exposed pad on the LCU-100/LSO-100 PCB layout. This is a layout requirement — verify against the Gerber before ordering.
- Post-reflow: confirm solder fillet is visible at the perimeter of the exposed pad on all four sides. If no fillet is visible, the thermal pad may not be soldered — verify with X-ray.
- Hand-rework of the thermal pad (if not soldered) requires hot-air rework station and copper wick. Do not attempt to reflow with iron only.

### 11.2 INA219 Current Sense Orientation

The INA219 measures current via a differential voltage across a shunt resistor. The orientation of IN+ and IN− relative to the current flow direction is critical:

- **IN+ must face the more positive node** (i.e., upstream / supply side of the shunt).
- **IN− must face the load side** of the shunt.
- An inverted INA219 will produce negative current readings under all conditions and positive-going current will read as negative — this will confuse the current monitoring logic.
- Verify pin 1 orientation against silkscreen **before placement** and confirm with post-reflow inspection.
- During functional test, apply a known current (e.g., 100 mA from a bench PSU) through the output and verify a positive reading via I2C.

### 11.3 Output Connector Crimping — TE Superseal 1.5

See Section 8.5. Additionally for LSO-100:

- The signal output channels connect to lamp circuits that may carry up to 5 A at 24 V DC (120 W per channel). Verify cable gauge matches the Superseal contact rating (1.5 mm contact rated to 13 A for 1 mm² cable).
- Label each Superseal connector with the channel number after assembly (permanent marker or heat-shrink label sleeve).

### 11.4 Output Inhibit Line

The output inhibit line (active-high enable from LCU-100, pulled high via pull-up on LCU-100 side) disables all BTS7030 outputs when de-asserted:

- **Before first power application** to LSO-100, verify the output inhibit line is at logic high (3.3 V) with a multimeter. If the line is floating or low, all outputs may energise unexpectedly on power-up.
- If testing LSO-100 standalone (without LCU-100), connect a 10 kΩ pull-up from the inhibit pin to 3.3 V before applying power.

---

## 12. LPB-100 Specific Assembly Notes

### 12.1 BQ76952 Decoupling Requirement

The BQ76952 TQFP-64 BMS IC requires low-impedance supply decoupling at every VCC and DVCC pin. Failure to populate or correctly solder these capacitors will cause BQ76952 power-on instability or ADC measurement errors:

- Place **10 µF MLCC + 100 nF MLCC** on every VCC pin group (refer to BQ76952 datasheet pin table and layout guidelines, SLUSC99).
- 10 µF capacitors: X5R or X7R dielectric, 16 V rating minimum, 0805 package (0603 acceptable if same footprint verified by BOM).
- 100 nF capacitors: X7R, 16 V, 0402.
- Verify all capacitor footprints are populated post-reflow (AOI will catch missing components).

### 12.2 MPPT Inductor Orientation — HCMA0703

The MPPT charger circuit (BQ25798) uses the HCMA0703 power inductor:

- The inductor winding direction and orientation affects radiated EMI.
- Refer to the BQ25798 reference layout (Texas Instruments SLVAFQ5) for recommended inductor placement and orientation relative to the IC.
- The switched node (SW pin) trace should be as short and wide as possible. Keep the SW node away from the GNSS and LTE RF traces. Maintain minimum 5 mm separation between the inductor and any RF antenna trace or connector.
- Verify inductor orientation (pin 1 to correct pad) against schematic — inductors do not have polarity for DC, but the winding start determines EMI radiation pattern per the datasheet recommendation.

### 12.3 Anderson SB50 Cable Dress — LPB-100

- The battery positive (B+) cable and CAN/RS-485 signal harness must be physically separated by a minimum of **20 mm** within the enclosure after installation.
- Route B+ cable along the bottom of the enclosure; route CAN/RS-485 harness along the top or along a dedicated cable duct.
- If 20 mm separation cannot be achieved, use an aluminium cable separator or route signal cables in a shielded braided sleeve.
- Refer to Section 4 of the Enclosure Integration Guide (LUM-MFG-002) for cable routing diagrams.

### 12.4 LPB-100 First Power-On — Pre-Charge Procedure

**Do not connect a fully charged battery to LPB-100 during first bring-up.** Follow this pre-charge procedure to safely verify the board before full battery connection:

1. Visually inspect LPB-100 for solder bridges, missing components, and obvious assembly errors.
2. Measure resistance between B+ and GND with a multimeter (resistance range). Expect > 10 kΩ (open circuit through BMS FETs). If < 1 Ω, there is a dead short — do not power on. Investigate before proceeding.
3. Connect a **bench power supply** set to the nominal cell stack voltage (e.g., 36 V or 48 V as configured), **current limited to 100 mA**.
4. Apply power. Monitor current draw. Expected draw at 100 mA limit: current will spike briefly on capacitor charge then settle to quiescent current (< 20 mA). If the PSU hits the 100 mA limit and stays there, there is a fault — remove power and investigate.
5. Verify LPB-100 output rail voltages with multimeter before connecting any load.
6. Only after successful pre-charge and quiescent checks should a battery be connected.

---

## 13. Firmware Flashing Procedure

### 13.1 LCU-100 Main Controller (STM32H743)

**Hardware:** ST-Link V3 debugger connected to J8 (SWD, 10-pin 1.27 mm Cortex Debug connector).

**Software:** OpenOCD 0.12.0 or later, STM32CubeProgrammer 2.14 or later.

**Step 1 — Flash bootloader:**

```
openocd \
  -f interface/stlink.cfg \
  -f target/stm32h7x.cfg \
  -c "program bootloader.hex verify reset exit"
```

Bootloader target address: 0x08000000 (start of internal Flash, Sector 0).

The bootloader provides:
- Firmware version reporting via USB CDC or CAN
- Firmware update via CAN (LUM protocol) or USB DFU
- Hardware self-test on power-up

**Step 2 — Flash application firmware:**

Using STM32CubeProgrammer GUI:
1. Connect via ST-Link.
2. Set start address to **0x08010000** (64 KB offset from Flash start; bootloader occupies Sector 0, 128 KB).
3. Open application .hex file.
4. Click Program.
5. Verify programming success — "File download complete" confirmation.

Or via CLI:

```
STM32_Programmer_CLI \
  -c port=SWD freq=4000 \
  -d application.hex 0x08010000 \
  -v \
  -hardRst
```

**Step 3 — Verify firmware:**

Connect to USB CDC serial port (115200 baud, 8N1) or via CAN service channel and confirm:
- Firmware version string returned matches expected version tag.
- Board reports correct hardware revision.
- Self-test passes (or lists expected failures for a bare board without sensors).

**Step 4 — Apply readout protection (before shipment):**

After validation, apply RDP Level 1 to prevent firmware readout:

STM32CubeProgrammer → OB (Option Bytes) → RDP → Level 1 → Apply.

Or via CLI:

```
STM32_Programmer_CLI \
  -c port=SWD \
  -ob RDP=1
```

**Warning:** RDP Level 2 is permanent and irreversible. Do **not** apply Level 2 to prototype boards. Use Level 1 only.

### 13.2 Safety Supervisor (STM32G071)

The STM32G071 acts as the independent watchdog and safety supervisor for the LCU-100. It monitors critical system signals and can disable outputs independently of the main STM32H743.

**Hardware:** ST-Link V3 via dedicated header J9 (if fitted to PCB revision) — 10-pin 1.27 mm SWD. On PCB revisions without J9, the SWD pins are accessible via the LCU-100 SWD multiplexer; select the G071 target via firmware or board-level jumper before connecting debugger.

**Flash procedure:**

```
openocd \
  -f interface/stlink.cfg \
  -f target/stm32g0x.cfg \
  -c "program safety_supervisor.hex verify reset exit"
```

Target Flash start address: 0x08000000 (STM32G071 has no separate bootloader in prototype; application loads directly).

**Apply RDP Level 1 immediately after flashing and verifying:**

```
STM32_Programmer_CLI \
  -c port=SWD \
  -ob RDP=1
```

**The safety supervisor firmware shall be read-protected before any board leaves the assembly area — no exceptions.** The supervisor logic is safety-relevant and must not be modifiable in the field.

### 13.3 Firmware Version Recording

Record in the production database:
- Bootloader version (semantic version + git commit SHA1, e.g., `1.0.0-abc1234`)
- Application firmware version (`1.0.0-def5678`)
- Safety supervisor firmware version (`1.0.0-fgh9012`)
- Date and operator of flashing

---

## 14. First-Article Inspection Checklist

The following checklist shall be completed and signed off by the assembly engineer for each board in the first prototype batch before the board is accepted for system integration.

**Board Type:** _____________ **Serial Number:** _____________ **Date:** _____________  
**Inspector:** _____________ **PCB Revision:** _____________ **BOM Revision:** _____________

| # | Inspection Item | Criterion | Pass | Fail | Notes |
|---|----------------|-----------|------|------|-------|
| 1 | Board dimensions | Within ±0.1 mm of fabrication drawing | ☐ | ☐ | |
| 2 | All components placed per BOM revision | No substitutions; all designators populated | ☐ | ☐ | |
| 3 | No solder bridges on fine-pitch ICs | STM32H743, BQ76952, BTS7030 — 0× bridges | ☐ | ☐ | |
| 4 | All polarised components correct orientation | INA219, diodes, electrolytic caps, ICs | ☐ | ☐ | |
| 5 | QFN/DFN thermal pads soldered (X-ray confirmed) | < 25% voiding | ☐ | ☐ | |
| 6 | QFP lead coplanarity within tolerance | ≤ 0.1 mm deviation on all leads | ☐ | ☐ | |
| 7 | Through-hole connectors soldered (Class 2 fillet) | 75% barrel fill minimum | ☐ | ☐ | |
| 8 | Anderson SB crimp contacts retained | No pullout at 50/100 N tug test | ☐ | ☐ | |
| 9 | Conformal coat coverage uniform (UV lamp) | No voids over safety-critical components | ☐ | ☐ | |
| 10 | Conformal coat absent from connector mating faces | Confirmed absent by UV and visual | ☐ | ☐ | |
| 11 | Serial number label applied and readable | Datamatrix and human-readable | ☐ | ☐ | |
| 12 | FRAM serial written and read-back verified | Serial matches label at 0xFFFF00 | ☐ | ☐ | |
| 13 | Firmware version confirmed via service port | All three firmware versions recorded | ☐ | ☐ | |
| 14 | RDP Level 1 applied (main + supervisor) | Confirmed via STM32CubeProgrammer | ☐ | ☐ | |
| 15 | Board powers up without rework | First power-on pass at nominal voltage | ☐ | ☐ | |
| 16 | All supply rails within ±2% of nominal | Measure at key test points per LUM-TEST-100 | ☐ | ☐ | |
| 17 | Functional test passed | LUM-TEST-100 all tests PASS | ☐ | ☐ | |
| 18 | Pre-charge procedure completed (LPB-100 only) | No faults at 100 mA current limit | ☐ | ☐ | |
| 19 | Output inhibit verified (LSO-100 only) | Inhibit line high before power-on confirmed | ☐ | ☐ | |
| 20 | Board registered in production database | Database entry complete with all fields | ☐ | ☐ | |

**Overall Result:** PASS ☐ / FAIL ☐ / CONDITIONAL PASS ☐ (attach deviation note)

**Inspector Signature:** _________________________ **Date:** _________________

**Engineering Approval (if FAIL or CONDITIONAL PASS):** _________________________ **Date:** _________________

---

*End of Document — LUM-MFG-001 Rev A*  
*Next review: at completion of first prototype batch or after any PCB revision change.*
