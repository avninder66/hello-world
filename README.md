# Lumina Traffic Systems — LCU-100 Controller Platform

## Overview

The LCU-100 Controller Platform is a modular temporary traffic signal controller designed for UK road works applications. It targets compliance with the TOPAS 2540A Portable Traffic Signal (PTS) pathway and is intended for 2-way to 4-way junction control, pedestrian crossings, and plant/site exit management.

The system is built around a 4-board architecture. Each board performs a distinct, separable function, allowing staged prototyping and field replacement:

| Board     | Full Name               | MCU           | CAN ID | Purpose                                          | Dimensions     |
|-----------|-------------------------|---------------|--------|--------------------------------------------------|----------------|
| LCU-100   | Main Controller Unit    | STM32H743VIT6 | 0x001  | Mission logic, timing plans, CAN bus master      | 100 × 80 mm    |
| LSO-100   | Signal Output Board     | None (slave)  | 0x010  | Lamp driver (230 VAC triacs), output monitoring  | 120 × 80 mm    |
| LPB-100   | Power & Battery Board   | STM32G071CBT6 | 0x020  | SMPS regulation, battery management, UPS         | 100 × 80 mm    |
| LPI-100   | Pedestrian Interface    | None (slave)  | 0x030  | Push-button input, audible/tactile outputs       | 80 × 60 mm     |

The safety supervisor MCU (STM32G071, on LPB-100) runs independently and holds a hardware inhibit line to all lamp drivers. It cannot be overridden by the main MCU via software alone.

---

## Repository Structure

```
lumina-controller/
├── docs/
│   └── architecture/         # System design documents
├── firmware/
│   ├── lcu/                  # STM32H743 main controller
│   ├── safety_supervisor/    # STM32G071 safety MCU
│   ├── common/               # Shared protocol headers
│   ├── bootloader/           # Secure bootloader
│   └── cmake/                # Build toolchain files
├── hardware/
│   ├── LCU-100/              # Main controller board
│   ├── LSO-100/              # Signal output board
│   ├── LPB-100/              # Power board
│   ├── LPI-100/              # Pedestrian interface
│   └── common/               # PCB stackup, component selection
└── manufacturing/
    ├── bom/                  # Bills of materials
    ├── test-procedures/      # Prototype test scripts
    └── assembly-notes/       # Build and integration guides
```

---

## Quick Start — Firmware Build

### Prerequisites

- `arm-none-eabi-gcc` >= 12.3 (e.g. from [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads))
- `CMake` >= 3.20
- `make` or `ninja`
- STM32CubeH7 HAL — place the `STM32H7xx_HAL_Driver` directory at `firmware/lcu/Drivers/STM32H7xx_HAL_Driver`
- FreeRTOS kernel — place at `firmware/lcu/Drivers/FreeRTOS`

### Build

```bash
cd firmware
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Build outputs:

```
build/lcu/lcu-firmware.hex
build/safety_supervisor/safety-supervisor-firmware.hex
```

A `Debug` build enables RTT logging via SEGGER J-Link and enables additional runtime assertions:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
```

---

## Flashing

Both MCUs are flashed via SWD using OpenOCD and a standard ST-LINK V2/V3 programmer. The LCU-100 board exposes a 10-pin ARM Cortex debug header (SWD + SWO + NRST).

### Flash LCU-100 (STM32H743)

```bash
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
  -c "program build/lcu/lcu-firmware.hex verify reset exit"
```

### Flash Safety Supervisor (STM32G071)

```bash
openocd -f interface/stlink.cfg -f target/stm32g0x.cfg \
  -c "program build/safety_supervisor/safety-supervisor-firmware.hex verify reset exit"
```

The bootloader occupies the first 64 KB of LCU-100 flash (0x08000000–0x0800FFFF). The application image starts at 0x08010000. Flashing `lcu-firmware.hex` writes both the bootloader and application; flashing the application image alone requires addressing from 0x08010000.

---

## Safety Architecture

The LCU-100 platform uses a dual-MCU safety design aligned with the intent of IEC 61508 SIL 1/2 decomposition. The design is not yet certified but is structured to support a future functional safety case.

**Main MCU (STM32H743 — LCU-100):** Runs the full timing plan engine, inter-board CAN protocol, and operator interface. It calculates phase outputs and requests lamp state changes.

**Safety Supervisor MCU (STM32G071 — LPB-100):** Runs independently on a separate power domain. It monitors:
- All-red inter-green periods (minimum guaranteed by hardware timer, not software)
- Simultaneous conflicting green detection via the hardware conflict matrix
- Watchdog heartbeat from the main MCU (loss of heartbeat triggers safe state)
- Supply voltage rails and battery state

**Hardware Inhibit Line:** The safety supervisor asserts a dedicated active-low `LAMP_INHIBIT` signal that connects directly to the enable inputs of all LSO-100 lamp driver triacs. This line bypasses the main MCU entirely. The main MCU has no ability to de-assert `LAMP_INHIBIT` through software; it can only assert it additionally.

**Conflict Matrix:** The LSO-100 board contains a combinational logic conflict matrix (implemented in discrete logic or a small CPLD, TBD at PCB stage) that prevents physically conflicting signals (e.g. opposing greens) from being simultaneously energised, regardless of what the CAN bus commands.

**Safe State:** Any detected fault causes both MCUs to drive all outputs to red (or lamp off if red lamp integrity is compromised), assert `LAMP_INHIBIT`, and broadcast a fault code on CAN.

---

## Standards Compliance Pathway

The LCU-100 platform is being developed against the following standards. Compliance is a development target, not a current claim.

| Standard              | Relevance                                                              | Status          |
|-----------------------|------------------------------------------------------------------------|-----------------|
| TOPAS 2540A           | UK Highways England / National Highways PTS type approval pathway      | In design       |
| BS EN 12368:2024      | Traffic control equipment — Signal heads                               | In design       |
| IEC 61000-4 series    | EMC immunity (ESD, EFT/burst, surge, radiated/conducted)               | Not yet tested  |
| IEC 61508 (intent)    | Functional safety framework — dual-MCU architecture aligned to SIL 1  | Structural only |
| BS 8442:2015          | Equipment for traffic management — portable signals, general           | Reviewing       |

Third-party EMC and functional safety review will be required before any highway trial.

---

## Development Status

| Stage | Description                                               | Status         |
|-------|-----------------------------------------------------------|----------------|
| 0     | Concept, architecture, design pack                        | Complete       |
| 1     | Schematic capture and PCB layout (all 4 boards)           | In progress    |
| 2     | Prototype PCB fabrication and bring-up                    | Not started    |
| 3     | Firmware bring-up, CAN protocol, basic signal cycling     | Not started    |
| 4     | Full timing plan engine, pedestrian interface, logging    | Not started    |
| 5     | EMC pre-compliance, environmental testing                 | Not started    |
| 6     | Independent functional safety review, TOPAS submission    | Not started    |

---

## Important Warnings

> **NOT CERTIFIED FOR PUBLIC HIGHWAY USE.**
> This design has not completed TOPAS 2540A type approval or any equivalent national certification process. It must not be deployed on a public highway.

> **HARDWARE BUILD SIGN-OFF REQUIRED.**
> The PCB designs contain mains voltage (230 VAC) lamp driver circuits. Any hardware build or assembly must be reviewed and signed off by a competent electronics engineer before powering up.

> **FUNCTIONAL SAFETY REVIEW REQUIRED.**
> Before any trial deployment — including on private land — the safety architecture must be independently reviewed against the intended SIL level and applicable standards.

> **COMMERCIAL IN CONFIDENCE.**
> This repository and all its contents are the confidential and proprietary property of Lumina Technology Ltd / Amber-RTM. Unauthorised disclosure, copying, or use is prohibited.

---

## Contact and Contributing

This repository is currently private and closed to external contributions.

For technical enquiries relating to the LCU-100 platform, contact the Lumina Technology Ltd engineering team.

Contributions from within the project team should follow the branch and review process defined in `docs/architecture/CONTRIBUTING.md` (to be created at Stage 1).
