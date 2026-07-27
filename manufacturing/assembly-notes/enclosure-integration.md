# Lumina Controller Platform — Enclosure Integration and Field Wiring Guide

**Document Number:** LUM-MFG-003  
**Revision:** A  
**Status:** Prototype / First Article  
**Applies To:** LCU-100, LSO-100, LPB-100, LPI-100 installed in standard enclosure  
**Prepared By:** Lumina Engineering  
**Date:** 2026-06-18  

---

## 1. Target Enclosure and Panel Preparation

### 1.1 Enclosure Specification

The primary target enclosure for the Lumina controller platform is a **GRP (Glass Reinforced Polyester) or ABS/PC IP65-rated enclosure**, minimum internal dimensions **400 mm (W) × 300 mm (H) × 150 mm (D)**.

Reference model: **Spelsberg TK PC 4030-15-m** (or equivalent from Fibox, Rittal, or Hammond with comparable internal dimensions and IP65 door seal certification). Key enclosure requirements:

| Parameter | Requirement |
|-----------|-------------|
| Material | GRP or UV-stabilised ABS/PC — not aluminium (galvanic isolation from PCB ground planes preferred in prototype) |
| IP rating | IP65 minimum when assembled with cable glands fitted |
| Internal W × H × D | 400 mm × 300 mm × 150 mm minimum (to accommodate all four boards plus cable routing clearance) |
| Door/lid seal | Continuous foam or rubber compression seal, replaceable |
| Mounting | Wall-mount or pole-mount bosses (25 mm mast clamp provisions preferred for traffic signal pole mounting) |
| Temperature rating | −20°C to +55°C operational (standard GRP/PC materials typically rated to −40°C, verify datasheet for UV resistance) |
| Flame rating | UL 94 V-0 minimum |
| Colour | Light grey (RAL 7035 preferred) to minimise solar heat gain |

### 1.2 Panel Drill Pattern

All external connectors, cable glands, and antenna mounts are installed in the enclosure base and side panels. The drill pattern must be marked and cut before board installation. Refer to the enclosure layout drawing (LUM-DRW-ENC-001) for dimensions; the following is a summary of required penetrations:

| Designation | Type | Quantity | Aperture | Location |
|-------------|------|----------|----------|----------|
| X1, X2 | M20 IP68 cable gland, nylon | 2 | Ø20.5 mm | Base panel — power cable ingress |
| X3–X6 | M20 IP68 cable gland, nylon | 4 | Ø20.5 mm | Base panel — signal and lamp output harnesses |
| X7 | M25 IP68 cable gland | 1 | Ø25.5 mm | Base panel — battery cable (larger OD) |
| CN1 | M12 5-pin A-coded CAN connector (panel mount) | 1 | Ø12 mm + 2× M3 clearance | Side panel, right |
| CN2 | M12 4-pin D-coded Ethernet (panel mount) | 1 | Ø12 mm + 2× M3 clearance | Side panel, right |
| CN3 | USB-C service port, IP67 cap | 1 | Ø12 mm | Side panel, left |
| ANT1 | GNSS antenna bulkhead SMA | 1 | Ø6.5 mm | Lid (top panel) — centred for sky view |
| ANT2 | LTE antenna bulkhead SMA | 1 | Ø6.5 mm | Side panel — minimum 100 mm from LPB-100 battery terminal |
| SW1 | IP67 keyswitch or pushbutton for power/arm | 1 | Ø22 mm | Front panel |
| PE | M4 earth stud, external | 1 | Ø4.5 mm | Base panel corner |
| PE | M4 earth stud, internal | 1 | Pre-tapped stud on enclosure boss | Internal left wall |

**Drilling notes:**
- Use a step drill or hole punch (not twist drill alone) for clean holes in GRP to avoid delamination around aperture edges.
- Seal cut GRP edges with epoxy primer to prevent moisture ingress into laminate.
- For ABS/PC enclosures, use a sharp 20 mm drill at low speed; support the panel from behind to prevent cracking.
- Apply thread-lock (medium strength, e.g., Loctite 243) to panel connector lock nuts where vibration is expected.
- Verify panel connector positions against PCB connector locations before drilling — M12 connector pigtails have a maximum length of 200 mm in the standard harness design.

### 1.3 Cable Gland Selection

Use **IP68-rated nylon cable glands** (e.g., Jacob or Skintop equivalents) sized for the actual cable OD:

| Cable Type | Approximate OD | Recommended Gland |
|-----------|---------------|------------------|
| Battery cable, 16 mm² | 12–15 mm OD | M25, IP68 |
| Power input / solar input cable, 4 mm² | 8–10 mm OD | M20, IP68 |
| Signal/CAN harness, 4-core shielded | 7–9 mm OD | M20, IP68 |
| Lamp output harness, 6-core | 10–12 mm OD | M20, IP68 |

Fit rubber strain relief bushings inside each gland; tighten gland nut to manufacturer torque (typically finger-tight plus 1.5 turns). Over-tightening can extrude the gland seal over-far and damage cable insulation. After tightening, tug cable with 50 N to confirm grip.

---

## 2. Board Mounting

### 2.1 Standoff Specification

All PCBs mount on **M3 stainless steel standoffs**, 6 mm nominal height (measured from enclosure floor panel to underside of PCB). Use:

- **Male–female hex standoffs** (M3 × 6 mm + M3 threaded stud, stainless steel A2) screwed into the enclosure floor bosses or into a mounting plate.
- **M3 × 8 mm stainless cap-head screws** to fix PCBs to standoffs from above.
- **M3 nylon washers** (1 mm thickness) between the PCB through-hole and the standoff top face where electrical isolation is required (see Section 2.2).
- Torque for M3 PCB mounting screws: **0.4–0.5 N·m** maximum. Do not over-torque — PCB material is not rated for high clamping forces.

### 2.2 Isolation Requirements

PCBs with a DC power ground that must be isolated from the enclosure chassis at the mounting point shall use nylon washers and nylon-sleeved M3 screws:

| Board | Chassis Isolation Required? | Notes |
|-------|-----------------------------|-------|
| LCU-100 | No (signal ground connected to chassis via PE pad only) | Standard steel standoffs acceptable |
| LSO-100 | No | Standard steel standoffs acceptable |
| LPB-100 | **Yes** — battery negative must not short to chassis through mounting hardware | Use nylon washers and nylon screw sleeves at all 4 mounting points; earth via dedicated PE wire only (Section 5) |
| LPI-100 | No | Standard mounting |

Verify isolation after assembly with a multimeter set to resistance: between LPB-100 main ground plane test point and enclosure chassis — should read > 1 MΩ (unmated to system) before PE bonding wire is connected.

### 2.3 Mounting Plate (Optional)

For production units, a **3 mm aluminium sub-plate** (400 × 280 mm, drilled to match all four board footprints) may be used as an intermediate mounting plate. This plate slides out of the enclosure for bench service without disconnecting all panel connectors. Prototype builds may mount directly to the enclosure floor.

---

## 3. PCB Mounting Order

Install boards in the following order to avoid access issues and ensure correct cable routing clearance:

| Order | Board | Location within Enclosure | Notes |
|-------|-------|--------------------------|-------|
| 1 | **LPB-100** — Power and Battery board | Lowest level, enclosure floor | Heaviest board (power components, choke); lowest position minimises cable run to battery gland |
| 2 | **LSO-100** — Signal Output board | Mid-level, above LPB-100 | Standoffs from enclosure floor; verify lamp output connector positions align with cable gland locations on base panel |
| 3 | **LCU-100** — Main Controller | Top level, above LSO-100 | Interconnect to LSO-100 via ribbon or harness; SWD/USB service connectors must face side panel |
| 4 | **LPI-100** — Pilot Interface | Lid or front panel | Typically mounted in the door/lid with panel-cut display window; cable harness connects to LCU-100 J-connector; must have sufficient cable length for door opening arc |

**Board-to-board clearance:** Maintain minimum **25 mm vertical clearance** between board surface and the underside of the board above it to allow for component height (tallest component on LPB-100 is the MPPT inductor at approximately 11 mm), cable routing, and thermal convection.

**Check before final installation:** with all boards in position, verify:
- No connector or component on one board is directly above a heat-generating component on the board below (BTS7030 PROFET on LSO-100 should not be directly below the STM32H743 on LCU-100 without 30 mm clearance).
- All connectors are accessible for harness routing and service.
- The SWD debug headers (J8, J9 on LCU-100) are not buried by the board above.

---

## 4. Cable Routing Rules

Correct cable routing within the enclosure is critical for EMC performance, safety, and service access. Non-conforming cable routing is a frequent source of field problems in traffic signal equipment.

### 4.1 Separation of Power and Signal Cables

| Cable Type | Segregation Rule |
|-----------|-----------------|
| Battery cables (B+, B−, 48 V, 16 mm²) | Route along enclosure base, dedicated channel |
| Solar input cables (12–48 V DC, 4 mm²) | Route adjacent to battery cables |
| Lamp output harness (0–24 V DC, multi-core) | Route on opposite side of enclosure from CAN/RS-485 |
| CAN bus (differential, shielded) | Minimum **50 mm physical separation** from battery/power cables, or use shielded harness with drain wire tied to chassis at one end |
| RS-485 (differential, shielded) | Same rule as CAN — 50 mm clearance from power cables |
| GNSS antenna coax (SMA to SMA, 50 Ω) | Route away from switched-mode power supplies; minimum 30 mm from inductor and PROFET switching nodes |
| LTE antenna coax | Route away from BQ25798 switching node and MPPT inductor; minimum 100 mm from battery terminal |

If the 50 mm separation cannot be achieved due to enclosure volume constraints:
- Option A: Use an aluminium cable separator strip (20 mm × 2 mm, fixed to enclosure base) as a physical barrier and EMC screen between power and signal bundles.
- Option B: Route signal cables in a shielded braided sleeve (minimum 85% optical coverage), with shield tied to chassis PE at the cable entry gland only (single-point grounding to prevent ground loops).

### 4.2 Cable Bundling

- Bundle cables with cable ties (nylon, UV-stabilised) at maximum 100 mm intervals.
- Do not overtighten cable ties — cables should not be pinched or deformed. Check jacket appearance after tying.
- Route cables to avoid sharp bending. Minimum bend radius: 5× cable diameter for power cables, 10× for RF coaxial cables.
- Leave a service loop (at least 150 mm) at each connector to allow disconnection and reconnection without cutting ties.
- LPI-100 door harness: allow for full door opening arc (typically 180° open). Service loop at hinge side: minimum 300 mm. Use corrugated conduit sleeve on the door hinge cable run.

### 4.3 Cable Identification

Label all internal cables at both ends:

- Use **self-laminating cable labels** (e.g., Brady B-7424) or **heat-shrink label sleeves** printed with function, voltage, and destination connector.
- Minimum label content: [FROM]-[TO]-[Function]-[Voltage/Signal type].
  - Example: `LPB1-LCU1-CAN-24V`
  - Example: `LPB1-BATT-B_POS-48V`

---

## 5. Earth Bonding and Protective Earth (PE)

Correct earth bonding is a safety requirement. All conductive parts within the enclosure that are accessible or could become live under fault conditions must be bonded to the protective earth (PE) stud.

### 5.1 External PE Stud

- **M4 × 12 mm stainless steel bolt** through the enclosure base panel, sealed with rubber washer under bolt head.
- External: connect to site/mains earth or vehicle chassis as applicable, using **green/yellow 4 mm²** cable minimum.
- The external earth point must be accessible without opening the enclosure.
- Mark with the standard earth symbol (IEC 60417-5019) engraved or labelled on the panel.

### 5.2 Internal PE Bonding

Each PCB has a designated PE pad (labelled PE on silkscreen, or 4 mm ring terminal lug hole). Bond each board to the enclosure internal earth stud with:

- **Green/yellow 2.5 mm² flexible stranded wire**.
- **Crimped M4 ring terminal** at both ends.
- Maximum bond wire length: 200 mm. Keep runs short and direct.
- Connection at enclosure: M4 internal stud (welded or threaded boss on enclosure wall), lock washer, M4 nut, torque to 1.2 N·m.

| Board | PE Bond Point | Note |
|-------|--------------|-------|
| LCU-100 | J-PE pad / M3 mounting hole labelled PE | Bond to enclosure wall stud |
| LSO-100 | Output connector chassis pad | Bond to enclosure wall stud |
| LPB-100 | Dedicated PE pad — isolated from B− | See Section 2.2; LPB-100 B− is not the same as chassis PE |
| LPI-100 | Frame/display chassis (if metallic) | If all-plastic, no bond required; verify with engineer |
| Enclosure door | Door frame to hinge | Short braided earth bond across hinge (50 mm, 2.5 mm² braid) |

**Important:** The LPB-100 battery negative (B−) is **not** connected to chassis PE. B− is a floating negative reference for the battery circuit. Connecting B− to chassis PE could introduce ground loops or create a fault current path that bypasses the BMS protection FETs. The PE bond on LPB-100 connects only to the enclosure safety earth, not to the battery circuit ground plane.

### 5.3 PE Continuity Test

After all PE bonds are made, and before system power-on, test PE continuity with a low-resistance ohmmeter:

- From external PE stud to each internal board PE pad: ≤ 0.1 Ω.
- From external PE stud to enclosure door frame (via hinge bond): ≤ 0.2 Ω.

Record results in the build checklist.

---

## 6. Connector Labelling

All external-facing connectors, cable glands, and penetrations must be permanently labelled. This is required for:
- Safe operation by field engineers who did not build the unit.
- Compliance with traffic signal equipment identification requirements.
- Fault finding and service.

### 6.1 Label Content

Each external connector or panel penetration shall carry a label identifying:

| Label Item | Required Information |
|-----------|---------------------|
| Function | What the connector does (e.g., "CAN BUS", "LAMP OUTPUT CH1–4", "BATTERY 48V") |
| Voltage and polarity | Nominal voltage, AC/DC, polarity for DC connectors (e.g., "+48V DC", "GND") |
| Connector type and pin-out reference | (e.g., "M12 A-coded, CiA 303-1") or drawing reference number |
| Caution text | "DO NOT CONNECT BATTERY REVERSED" on SB50; "ISOLATE BEFORE SERVICING" on lamp outputs |

### 6.2 Label Material

- **Primary method (production):** Engraved aluminium labels, adhesive-backed or rivet-mounted, 1.5 mm anodised aluminium, laser or mechanical engraving. Suitable for outdoor use and impervious to chemicals and UV.
- **Acceptable for prototype:** P-touch or equivalent industrial label printer on polyester labels (Dymo or Brady M21 series). Polyester labels are rated for −30°C to +80°C and UV-resistant. Avoid paper labels — they will delaminate in moisture.
- Labels must be positioned adjacent to (not covering) the connector being labelled.
- Verify label adhesion on GRP surface — clean surface with IPA before applying; press firmly for 30 seconds.

### 6.3 Warning Labels

The following warning labels shall be applied to the enclosure exterior:

| Text | Location |
|------|----------|
| "CAUTION — TRAFFIC SIGNAL CONTROLLER — AUTHORISED PERSONNEL ONLY" | Lid/door outer face |
| "BATTERY VOLTAGE 48V DC — ISOLATE BEFORE OPENING" | Door, adjacent to latch |
| "MAINTAIN EARTH CONTINUITY BEFORE RECONNECTING POWER" | Adjacent to external PE stud |

---

## 7. Antenna Placement

Antenna placement significantly affects the signal quality of the GNSS and LTE systems. Follow these guidelines strictly — poor antenna placement is a common cause of GNSS cold-start failures and LTE connectivity problems in the field.

### 7.1 GNSS Antenna (ANT1)

- Mount a **flush-mount active GNSS patch antenna** in the **centre of the lid (top panel)** of the enclosure.
- The antenna must have a **clear, unobstructed view of the sky** — a minimum 70° half-angle cone above the antenna surface shall be free of metal, GRP, and solid structures. GRP enclosure lids are transparent to GNSS frequencies (L1 1575.42 MHz) and do not require a cutout, but metal lids would require an external antenna with coaxial cable.
- Active patch antenna (built-in LNA): requires 3.3 V or 5 V bias supplied via coaxial cable. Verify LCU-100 GNSS circuit provides the correct bias voltage on the antenna coax centre conductor; measure before connecting antenna.
- **Minimum 100 mm** from any LTE antenna or LTE cable to avoid IMD (intermodulation distortion).
- Orient antenna parallel to PCB (horizontal polarisation preferred for satellite reception).
- Bulkhead SMA connector in lid: connect with an appropriate length of low-loss 50 Ω coaxial cable (RG-174 or similar for prototype; consider low-loss LMR-100A if cable length > 300 mm to minimise GNSS signal path loss).

### 7.2 LTE Antenna (ANT2)

- Mount on the **side panel** of the enclosure.
- **Minimum 100 mm from battery terminals** and battery cable routes (switching interference from battery charge/discharge cycles can couple into the LTE antenna at close range).
- **Minimum 50 mm from any other antenna** (GNSS, or a second LTE antenna for MIMO if applicable).
- Preferred position: upper half of side panel for best radiation pattern in typical pole-mounting orientation.
- External LTE whip antenna (50 Ω, right-angle SMA) is preferred for pole-mounted enclosures — ensure antenna clears the mounting pole and is oriented vertically.
- If using an internal LTE antenna (PCB-trace or adhesive patch), ensure no metallic components within 10 mm of antenna radiating element.

### 7.3 Antenna Coaxial Cable Routing

- Use the correct coaxial cable type (50 Ω) for the frequency and cable length.
- All coaxial cables must be secured with cable ties at maximum 100 mm intervals inside the enclosure.
- Coaxial cable minimum bend radius: 10× cable OD. Never kink or crush coaxial cables.
- Verify SMA connector torque at both ends: **0.9 N·m** (hand-tight plus approximately 1/8 turn with spanner — do not over-torque SMA connectors as centre pin deformation will increase VSWR).
- After installation, verify antenna connectivity: for GNSS, check the LCU-100 firmware reports GNSS lock (or at least signal detection) within 5 minutes outdoors in an open area. For LTE, verify EC21 module reports network registration.

---

## 8. Thermal Management

### 8.1 Passive Thermal Design

The Lumina controller platform is designed for **passive (natural convection) cooling** with no forced-air ventilation. This is appropriate for outdoor temporary traffic signal equipment (no fans, no additional failure modes, maintains IP65). Verify the following conditions are met to ensure passive cooling is adequate:

**Component derating:** At the expected maximum ambient temperature of 55°C (direct sun on a grey GRP enclosure may add 10–15°C internal temperature rise in worst case; design for 70°C internal ambient):

| Component | Max Junction Temp | Derating Target | Notes |
|-----------|------------------|-----------------|-------|
| BTS7030-2EPA | 175°C (Tj max) | < 125°C Tj at 70°C ambient | Thermal pad must be soldered correctly; derate by 50% of rated current if enclosure ambient exceeds 60°C |
| STM32H743 | 125°C (Tj max) | < 105°C Tj | Typically limited by enclosure temperature, not self-heating |
| BQ76952 | 85°C (Tj max, TQFP) | < 80°C Tj | Low self-heating in normal operation |
| BQ25798 | 125°C (Tj max) | < 100°C Tj | Thermal pad soldering critical; verify in thermal camera test |

**Board clearance:** Maintain minimum **25 mm clearance from PCB edge to enclosure wall** to allow for convection air circulation. Do not fill the enclosure interior completely with boards and cabling.

### 8.2 Thermal Test Procedure

During prototype bring-up, perform a **thermal soak test** at maximum rated ambient:

1. Install all boards in the enclosure, fully assembled and operational.
2. Apply rated load on lamp outputs (e.g., full dummy load on all 8 LSO-100 channels).
3. Operate in an environmental chamber at 55°C ambient (or in direct sun outdoors as a worst-case).
4. Run at full load for 2 hours.
5. Use thermal imaging camera to measure component temperatures through the GRP lid (GRP has high emissivity; readings will be approximate — add 5°C margin to IR readings compared with thermocouple).
6. If any component exceeds its derating target, implement one of:
   - Increase internal volume (larger enclosure model).
   - Add thermal interface material (TIM) between hot component and enclosure wall (if PCB is close enough).
   - Provision for heat-sink fin (see Section 8.3).

### 8.3 Heat Sink Provisions

The LPB-100 and LSO-100 PCB layouts shall include **M6 PEM nut provisions** (press-fit M6 threaded inserts in the PCB or enclosure mounting plate) at two locations per board, accessible from the enclosure exterior base panel. These provisions allow a **bolt-on aluminium fin heat sink** to be added post-production if thermal testing indicates excessive junction temperatures in field use.

At the prototype stage, heat sink fins are not fitted. The PEM nut provisions are reserved for future use if required by thermal test results.

---

## 9. IP Sealing and Weatherproofing Verification

### 9.1 Enclosure Door Seal Inspection

Before every deployment, inspect the enclosure door seal:

- The continuous foam or rubber seal around the door perimeter must be:
  - Unbroken (no cuts, tears, or missing sections)
  - Clean (no debris or dirt in the seal groove)
  - Undistorted (no permanent set or compression creep that would leave gaps)
- Close the door and check for uniform gap around the perimeter. Use a 0.1 mm feeler gauge to check compression — if the feeler gauge passes anywhere under the seal, the seal is not compressing adequately.
- If seal integrity is in doubt, replace the door seal before field deployment. Spelsberg and equivalent enclosure manufacturers supply replacement seal kits.

### 9.2 Cable Gland Verification

Each cable gland must be verified as follows:

- Gland nut tightened to specification (finger-tight plus 1.5 turns, or manufacturer torque specification).
- Cable secured — apply 50 N axial pull; cable must not slide in gland.
- Any unused cable gland entries must be fitted with a **solid blanking plug** (threaded nylon or metal blanking plug rated to IP68). Do not leave open gland entries or gland holes without a plug — this immediately voids IP rating.
- Glands for multi-core cables with shields: thread the shield drain wire through the gland and connect to the enclosure PE earth bond point. Tighten gland around the outer cable jacket, not the shield.

### 9.3 IP65 Water Spray Test

Before the first field deployment of each assembled unit, perform a simplified IP65 spray test:

1. Ensure all cable glands are tightened, blanking plugs fitted, and door sealed.
2. Using a standard garden spray nozzle or IP65 spray test nozzle at 3 metres range, spray water over all surfaces of the closed enclosure for a minimum of 3 minutes. Rotate the enclosure to expose all faces if the size permits; otherwise spray from multiple angles.
3. Open the enclosure immediately after the spray test. Inspect interior with a torch for any water ingress. Any water inside is a failure.
4. If water ingress is found, identify the ingress point (wet streak marks will indicate direction). Common failure points: cable gland insufficiently tightened; door seal debris; panel connector lock nut not tightened.
5. Record the spray test result in the unit build record.

**Note:** IP65 is sufficient for outdoor temporary traffic signal use (protection against water spray from any direction). If the application requires submersion resistance (e.g., flooded road scenario), upgrade to IP67 or IP68-rated enclosure — the Spelsberg TK PC range is also available in IP66 and IP67 versions at the same physical dimensions.

---

## 10. Vibration Isolation

### 10.1 Background

Temporary traffic signal controllers are transported between sites frequently — loaded onto trailers, flatbeds, or vans — and may be subject to road vibration, pot-hole impacts, and handling shocks. Without vibration isolation, PCB solder joint fatigue, connector fretting, and intermittent connection failures are common causes of field failure in traffic signal equipment.

### 10.2 Anti-Vibration Mounts

Install **rubber anti-vibration grommets** between each standoff/PCB mounting point and the enclosure base:

- Type: **M3 rubber anti-vibration bush** (e.g., Lanber or Enidine M3 grommet mount). These are bonded rubber/metal inserts with M3 female thread on both sides.
- Durometer: 40–50 Shore A (medium stiffness) — softer than 40 Shore A may allow excessive PCB movement under the weight of connectors and may fatigue the rubber under sustained high-frequency vibration.
- Rated static load: minimum 10 N per mount. With 4 mounts per board and a board mass of approximately 200 g, the design margin is adequate at this stiffness.
- Natural frequency of the isolated system: target 15–30 Hz first resonance (above road noise spectrum of 5–12 Hz, below shock impulse range of 50+ Hz).

**Installation:** Stack order at each mounting point (from enclosure floor upward):

```
Enclosure floor boss (M3 threaded)
  └── Anti-vibration grommet (M3 × 6, rubber/steel)
        └── 6 mm steel standoff (M3 male-female)
              └── Nylon washer (where isolation required)
                    └── PCB through-hole
                          └── M3 cap-head screw
```

### 10.3 Vibration Test Requirements

For prototype validation, subject one assembled unit to the following transport vibration profile (based on IEC 60068-2-64 random vibration, road vehicle profile):

| Axis | Vibration Level | Duration |
|------|----------------|---------|
| Z (vertical) | 0.5 g RMS, 5–50 Hz | 30 minutes |
| X and Y (horizontal) | 0.25 g RMS, 5–50 Hz | 15 minutes each |

After vibration test:
- Inspect all solder joints under 10× magnification for fatigue cracks (particularly at through-hole connector legs and large IC corners).
- Check all M12 panel connectors for loosening.
- Verify the unit powers up and passes functional test without rework.
- Record results.

For production, a **resonance sweep (sinusoidal sweep 5–100 Hz, 1 g, 1 oct/min)** should be performed to identify any enclosure or PCB resonances before committing to full vibration testing.

### 10.4 Connector Retention in Vibration Environment

- All internal connectors subject to vibration (harness connectors between boards, power connectors) shall have secondary retention:
  - Molex MiniFit Jr.: verify locking tab fully engaged; apply a single wrap of self-amalgamating tape over the mated connector pair on critical power connections (B+, 24 V lamp supply) to prevent vibration-induced unmating.
  - Superseal 1.5: housing locks provide primary retention; no secondary fixation required.
  - SMA coaxial: tighten to 0.9 N·m; apply thread-lock (low-strength, e.g., Loctite 222) to SMA nut after torquing.
  - M12 panel connectors: locking ring must be fully tightened; add a cable tie around the connector body to prevent rotation of the locking ring under vibration.

---

## Appendix A — Enclosure Build Checklist

The following checklist shall be completed and signed by the integration engineer for each assembled unit before field deployment.

**Unit Serial Number:** _____________ **Date:** _____________ **Engineer:** _____________

| # | Item | Criterion | Pass | Fail |
|---|------|-----------|------|------|
| 1 | Enclosure drill pattern complete | All apertures per LUM-DRW-ENC-001 | ☐ | ☐ |
| 2 | Cut GRP edges sealed with primer | No exposed cut laminate | ☐ | ☐ |
| 3 | All boards mounted and secured | Correct standoff heights; torque verified | ☐ | ☐ |
| 4 | LPB-100 mounting isolation verified | > 1 MΩ B− to chassis | ☐ | ☐ |
| 5 | PCB mounting order correct | LPB-100 bottom, LSO mid, LCU top, LPI lid | ☐ | ☐ |
| 6 | Anti-vibration grommets fitted | All mounting points | ☐ | ☐ |
| 7 | Power/signal cable separation ≥ 50 mm | Or shielded harness in use | ☐ | ☐ |
| 8 | Cable bends within minimum radius | Power ≥ 5× OD; RF ≥ 10× OD | ☐ | ☐ |
| 9 | All cables labelled at both ends | Function, voltage, destination | ☐ | ☐ |
| 10 | PE bonds all fitted and verified | ≤ 0.1 Ω board PE to external stud | ☐ | ☐ |
| 11 | B− isolated from chassis PE | Verified with ohmmeter | ☐ | ☐ |
| 12 | All external connectors labelled | Function, voltage, polarity, caution text | ☐ | ☐ |
| 13 | Warning labels applied to exterior | Three labels per Section 6.3 | ☐ | ☐ |
| 14 | GNSS antenna in lid centre | Clear sky view confirmed | ☐ | ☐ |
| 15 | LTE antenna ≥ 100 mm from battery | Measured and confirmed | ☐ | ☐ |
| 16 | Antenna coax torqued (0.9 N·m) | Both ends of each cable | ☐ | ☐ |
| 17 | Cable glands tightened; cables retained at 50 N | All glands | ☐ | ☐ |
| 18 | Unused gland holes blanked (IP rated) | No open entries | ☐ | ☐ |
| 19 | Door seal intact and clean | No gaps under feeler gauge | ☐ | ☐ |
| 20 | IP65 spray test passed | No water ingress after 3-minute spray | ☐ | ☐ |
| 21 | GNSS lock obtained in open sky | Within 5 minutes outdoors | ☐ | ☐ |
| 22 | LTE network registration confirmed | EC21 reports network registration | ☐ | ☐ |
| 23 | Full system functional test passed | Per LUM-TEST-100 system test procedure | ☐ | ☐ |

**Overall Result:** PASS ☐ / FAIL ☐

**Engineer Signature:** _________________________ **Date:** _________________

---

## Appendix B — Torque Reference Summary

| Fastener / Connection | Torque |
|-----------------------|--------|
| M3 PCB mounting screw | 0.4–0.5 N·m |
| M4 PE bond stud nut | 1.2 N·m |
| M12 panel connector nut | 0.6–0.8 N·m |
| SMA coaxial connector nut | 0.9 N·m |
| PCB screw terminal | 0.5–0.6 N·m |
| Cable gland nut (M20, M25 nylon) | Finger-tight + 1.5 turns (see gland datasheet) |
| Anderson SB50 cable retention screw (if applicable) | Per Anderson datasheet |

---

*End of Document — LUM-MFG-003 Rev A*  
*Next review: on completion of first prototype field trial or after any enclosure layout change.*
