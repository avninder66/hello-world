# Lumina Traffic Signal Controller — CLAUDE.md

## Project Overview

Embedded firmware and hardware design for a UK temporary traffic signal controller. This is a safety-critical product targeting TOPAS 2540A approval. The platform is modular, comprising four boards:

- **LCU-100** — Central Control Unit (STM32H743 application MCU + STM32G071 safety supervisor)
- **LSO-100** — Signal Output board (lamp drive channels, conflict detection hardware)
- **LPB-100** — Power Bus board (12V distribution, battery management)
- **LPI-100** — Pedestrian Interface board (push buttons, tactile cone, audible tones, ped signals)

All inter-board communication uses CAN FD at 1 Mbps (arbitration) / 4 Mbps (data phase).

## Repository Layout

```
firmware/               C firmware, CMake build system, FreeRTOS-based application
  lcu/                  LCU-100 application MCU firmware (STM32H743)
  safety_supervisor/    LCU-100 safety supervisor firmware (STM32G071, bare-metal)
  lpi/                  LPI-100 local MCU firmware (STM32G031)

hardware/               KiCad netlists, PCB stackup specs, component selection
  LCU-100/schematics/   LCU-100 schematic netlist
  LSO-100/schematics/   LSO-100 schematic netlist
  LPB-100/schematics/   LPB-100 schematic netlist
  LPI-100/schematics/   LPI-100 schematic netlist
  common/               Shared footprint libraries, design rules
  bom/                  Bill of materials (CSV per board)
  pcb/                  PCB layer stackup specs, impedance targets

docs/                   Architecture documents, safety concept
  LUM-ARCH-ELEC-001     Electrical architecture document
  safety-concept/       Functional safety concept (TOPAS 2540A pathway)

manufacturing/          BOMs (CSV), test procedures, assembly guidelines
  bom/                  Per-board BOM CSV files
  test/                 Factory test procedures
  assembly/             Assembly instructions and drawings
```

## Firmware Build Requirements

- **Toolchain**: `arm-none-eabi-gcc` >= 12.3.0 — obtain from the ARM Developer website (https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads), **not** from `apt`. The `apt` version is typically too old.
- **CMake** >= 3.20
- **Python 3** for build scripts (version 3.9+ preferred)
- **STM32CubeH7 HAL library**: must be placed at `firmware/lcu/Drivers/STM32H7xx_HAL_Driver/`
- **FreeRTOS kernel**: must be placed at `firmware/lcu/Drivers/FreeRTOS-Kernel/`
- **STM32CubeG0 HAL**: must be placed at `firmware/safety_supervisor/Drivers/STM32G0xx_HAL_Driver/`
- **STM32CubeG0 HAL** (for LPI): must be placed at `firmware/lpi/Drivers/STM32G0xx_HAL_Driver/`

These HAL and RTOS directories are not committed to the repository. Download them from ST and the FreeRTOS GitHub before building.

Typical build sequence:

```sh
cd firmware/lcu
cmake -B build -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi.cmake
cmake --build build
```

## Key Architecture Decisions

| Decision | Rationale |
|---|---|
| Safety MCU (STM32G071) runs bare-metal superloop only | No FreeRTOS, no dynamic allocation — determinism and auditability for safety |
| Application MCU (STM32H743) never directly drives lamp outputs | All output requests go via safety MCU; the safety MCU holds veto and conflict-check authority |
| FRAM for fault logging (not Flash or EEPROM) | Write endurance: FRAM is rated 10^13 cycles vs. ~10^5 for Flash/EEPROM |
| CAN FD at 1 Mbps / 4 Mbps | Meets inter-board latency budget; ISO 11898-2 compliant; hardware-terminated at endpoints |
| All phase timing enforced in safety MCU | Phase timers in STM32G071, not STM32H743 — application MCU cannot accelerate or skip phases |
| LPI-100 has its own local MCU (STM32G031) | Debounce, CAN framing, and audio generation offloaded; reduces LCU CAN bus traffic |

## Coding Standards

- **Language standard**: C11
- **No dynamic memory allocation** in the safety supervisor (`firmware/safety_supervisor/`) — no `malloc`, `calloc`, `realloc`, or `free` anywhere in that tree
- **All safety supervisor functions must be deterministic** — no loops with unbounded iteration, no recursion
- **FreeRTOS heap size** is fixed at compile time via `configTOTAL_HEAP_SIZE` in `FreeRTOSConfig.h`; never increase it without reviewing stack/task budgets
- Never call `HAL_Delay()` in a FreeRTOS context — always use `vTaskDelay()` or `vTaskDelayUntil()`
- `static` qualifier on all file-scope symbols that are not part of a public module API
- `const` on all pointer parameters that are not written through
- No implicit fall-through in `switch` statements — use `/* fallthrough */` comment where intentional

## File Naming Conventions

| Artefact | Pattern | Location |
|---|---|---|
| Header files | `module_name.h` | `App/Inc/` or `Core/Inc/` within the relevant firmware tree |
| Source files | `module_name.c` | `App/Src/` or `Core/Src/` |
| BOM files | `{BOARD_ID}-BOM.csv` | `manufacturing/bom/` |
| Schematic netlists | `{BOARD_ID}-schematic-netlist.net` | `hardware/{BOARD_ID}/schematics/` |
| Test procedures | `{BOARD_ID}-test-procedure-vX.Y.md` | `manufacturing/test/` |

Examples: `LCU-100-BOM.csv`, `LPI-100-schematic-netlist.net`, `phase_plan.h`

## Important Safety Notes

**These rules must not be violated without a full safety review:**

1. **Never add code that allows the application MCU (STM32H743) to directly set GPIO outputs that drive lamp channels.** All lamp drive commands must be sent as CAN FD messages to the safety MCU, which performs conflict checking before acting.

2. **The inhibit line (`INHIBIT_OUT` on the safety MCU) must default active-low (inhibited) on power-up.** The safety MCU must explicitly release inhibit after completing its self-test sequence. Any code change that alters this power-up default requires a safety review.

3. **The conflict matrix must be updated in both `conflict_matrix.c` AND the safety concept document** (`docs/safety-concept/`) if signal phase approaches are changed. The two must remain consistent — the document is the authoritative source, the code is its implementation.

4. **Any change to timing constants in `phase_plan.h` must be reviewed against TOPAS 2540A requirements** before committing. Minimum all-red intergreen times in particular may not be reduced without formal re-approval.

5. **The LPI-100 (STM32G031) must not directly control pedestrian reds/greens** without a valid CAN FD authorisation frame from the LCU-100. Locally cached phase state is used only for graceful degradation (all-red fallback), never for normal phase progression.

## Standards References

| Standard | Scope |
|---|---|
| **TOPAS 2540A** (mandatory) | Performance specification for portable/temporary traffic signals — primary approval pathway |
| **BS EN 12368:2024** | UK/EU signal head specification (lens colours, luminous intensity, flash rates) |
| **IEC 61000-4 series** | EMC immunity: ESD, EFT/burst, surge, conducted RF, magnetic field (design inputs for LPB/LCU hardware) |
| **IPC-A-610 Class 2/3** | PCB assembly quality standard (Class 3 target for safety-critical boards LCU/LSO) |
| **ISO 11898-2:2016** | CAN physical layer (CAN FD bus, differential signalling, termination) |
| **ARM Cortex-M**: cortex-m7 (STM32H743), cortex-m0plus (STM32G071, STM32G031) | GCC target architecture flags for firmware builds |

## Not Yet Implemented

The following are planned but not yet present in the repository:

- **LRM-100** radio/LoRa daughterboard — hardware design and firmware not started
- **Phase 2 pedestrian presence detection** — radar/thermal sensor integration (architecture TBD)
- **OTA firmware update via LTE** — secure bootloader and update server not designed
- **Commissioning mobile app** — BLE interface on LCU-100 (hardware pads reserved, firmware absent)
- **TOPAS 2540A formal test plan** — test procedures in `manufacturing/test/` are draft only; formal witnessed testing not yet scheduled
- **HSE clock fault handling** in LPI-100 firmware — STM32G031 currently uses internal RC oscillator only; HSE crystal (if fitted) not configured
