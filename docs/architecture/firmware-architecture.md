# Lumina LCU-100 Firmware Architecture

**Document Reference:** LCU-100-FW-ARCH-001  
**Revision:** 1.0  
**Status:** Released for Engineering  
**Date:** 2026-06-18  
**Author:** Lumina Engineering  
**Classification:** Confidential — Engineering Internal

---

## Table of Contents

1. Overview and Design Philosophy
2. Memory Map (STM32H743)
3. FreeRTOS Task Architecture
4. Inter-Task Communication
5. Traffic Engine State Machine
6. Safety Supervisor Protocol (SPI)
7. CAN FD Bus Protocol
8. Secure Boot and Firmware Update
9. RTOS Watchdog Architecture
10. Logging and Diagnostics
11. Non-Safety Communications Architecture

---

## 1. Overview and Design Philosophy

### 1.1 Purpose

This document describes the firmware architecture of the Lumina LCU-100 Traffic Signal Controller. It is intended for use by embedded software engineers, safety assessors, and technical reviewers involved in the design, verification, and certification of the LCU-100 platform.

### 1.2 Dual-MCU Architecture

The LCU-100 employs a **dual-microcontroller architecture** as the foundational safety mechanism. The two processors have strictly separated responsibilities and communicate only through a defined, integrity-protected SPI protocol.

| Processor | Device | Role | RTOS | Clock Source |
|---|---|---|---|---|
| Application MCU | STM32H743ZIT6 | Traffic logic, communications, telemetry, diagnostics | FreeRTOS v10.5.1 | 480 MHz (PLL from HSE 25 MHz TCXO) |
| Safety MCU | STM32G071RBT6 | Output permissive control, conflict matrix, intergreen timers | Bare-metal superloop | 64 MHz (internal RC oscillator — HSI) |

The Safety MCU uses its own internal RC oscillator intentionally. This ensures that a failure of the external crystal oscillator on the Application MCU cannot affect the Safety MCU clock, preserving the independence of the safety function.

### 1.3 Design Philosophy

The central principle of the LCU-100 firmware is **asymmetric authority**: the Application MCU requests phase changes, but it can never directly energise a signal output. All output enable decisions are made exclusively by the Safety MCU based on its own evaluation of the conflict matrix, intergreen state, and inhibit conditions.

Key design decisions arising from this philosophy:

- **No shared output control registers.** The Application MCU has no electrical or software path to directly control BTS7030 output channels. Output enables are routed only through the Safety MCU inhibit GPIO chain.
- **Hardware inhibit by default.** The inhibit line is active-low, open-drain, and pulled to ground by hardware at power-on. The Safety MCU must actively drive it high to permit outputs. A failure of the Safety MCU removes the drive, and the line returns to the inhibited state.
- **SPI heartbeat with timeout.** The Safety MCU monitors SPI frame validity and timing. Loss of the Application MCU heartbeat causes inhibit assertion within 30 ms, independent of the Application MCU watchdog.
- **Safety code never calls application code.** The Safety MCU firmware contains no dependencies on any library or module shared with the Application MCU. Compilation is entirely separate.
- **Non-safety code may fail without causing a hazard.** LTE telemetry, GNSS, diagnostic logging, and commissioning services may fail gracefully without triggering safety responses, provided the SPI heartbeat and phase command channels remain healthy.

### 1.4 Software Component Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                    Application MCU (STM32H743)                  │
│                                                                 │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────────────┐ │
│  │ Traffic      │  │ CAN FD Stack │  │ Telemetry / LTE / MQTT│ │
│  │ Engine FSM   │  │ (FDCAN 1/2)  │  │ (Quectel EC21 USART3) │ │
│  └──────┬───────┘  └──────┬───────┘  └───────────────────────┘ │
│         │                 │                                     │
│  ┌──────▼─────────────────▼───────────────────────────────────┐│
│  │              FreeRTOS Kernel + IPC primitives              ││
│  └───────────────────────────────────┬─────────────────────── ┘│
│                                      │ SPI (10 ms cycle)        │
└──────────────────────────────────────┼─────────────────────────┘
                                       │
┌──────────────────────────────────────▼─────────────────────────┐
│                    Safety MCU (STM32G071)                       │
│                                                                 │
│  ┌─────────────────┐  ┌──────────────┐  ┌────────────────────┐ │
│  │ SPI Frame        │  │ Conflict     │  │ Phase Timer /      │ │
│  │ Validator        │  │ Matrix       │  │ Intergreen FSM     │ │
│  └──────────────────┘  └──────────────┘  └────────────────────┘ │
│         │                                         │             │
│  ┌──────▼─────────────────────────────────────────▼──────────┐ │
│  │           Inhibit GPIO Driver (active-low, open-drain)    │ │
│  └───────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. Memory Map (STM32H743)

### 2.1 Flash Memory Layout

The STM32H743 has 2 MB of dual-bank Flash. The LCU-100 uses a dual-slot bootloader scheme, reserving Slot B as a safe factory fallback image.

| Region | Base Address | Size | Contents |
|---|---|---|---|
| Bootloader | `0x08000000` | 64 KB | First-stage bootloader (never updated OTA) |
| Application Slot A | `0x08010000` | 960 KB | Active application firmware |
| Application Slot B | `0x08100000` | 1 MB | OTA staging / safe factory fallback |

**Notes:**
- The bootloader occupies the first eight 8 KB sectors of Bank 1. It is write-protected in RDP Level 1 configuration.
- Slot A and Slot B cannot both be active simultaneously. The bootloader selects which slot to launch based on image header validity and consecutive failure counters.
- Slot B holds the last known-good factory image until a successful OTA update is committed. If three consecutive Slot A boot attempts fail (watchdog reset within 10 s of boot), the bootloader falls back to Slot B.

### 2.2 RAM Usage (STM32H743 Internal SRAM)

| Region | Base Address | Size | Assigned Usage |
|---|---|---|---|
| DTCMRAM | `0x20000000` | 128 KB | FreeRTOS task stacks, TCBs, kernel data |
| AXI SRAM (RAM_D1) | `0x24000000` | 512 KB | Heap, application data, traffic engine buffers |
| SRAM1/2 (RAM_D2) | `0x30000000` | 288 KB | DMA buffers, CAN FD mailbox mirrors, LTE receive buffer |
| SRAM4 (RAM_D3) | `0x38000000` | 64 KB | RTC backup domain, low-power retained state |
| Backup SRAM | `0x38800000` | 4 KB | Boot attempt counters, reset-cause flags |

DTCMRAM is chosen for stacks and TCBs because it is accessible at zero wait states by the Cortex-M7 core, reducing worst-case ISR latency. DMA-capable peripherals (FDCAN, USART3 for LTE) require buffers in RAM_D2 as DTCMRAM is not accessible by the DMA bus matrix.

### 2.3 External FRAM Memory Map (Fujitsu MB85RS4MT, 512 KB)

FRAM is connected to the Application MCU via SPI2 and provides non-volatile storage for fault logs and configuration. The shared SPI bus is arbitrated by `xFRAMMutex`.

| Region | Address Range | Size | Contents |
|---|---|---|---|
| Fault Log Ring Buffer | `0x000000` – `0x07FFFF` | 512 KB | Fault log entries (ring buffer, 16,000 × 32 B) |
| Phase Configuration | `0x010000` – `0x011FFF` | 8 KB | Active phase timing configuration |
| Serial / Calibration Data | `0xFF0000` – `0xFF00FF` | 256 B | Unit serial number, hardware revision, calibration offsets |

**Note:** The fault log ring buffer occupies the entire FRAM address space. Phase configuration and calibration data are stored in the upper address range, which is excluded from the ring buffer write pointer wrap. A compile-time assertion enforces that the ring buffer pointer never reaches `0x010000`.

---

## 3. FreeRTOS Task Architecture

### 3.1 Task Summary

FreeRTOS v10.5.1 is configured with `configUSE_PREEMPTION = 1`, `configUSE_TIME_SLICING = 0`, and `configTICK_RATE_HZ = 1000` (1 ms tick). The tick is driven by TIM6 to avoid conflict with the IWDG and LPTIM peripherals.

| Task Name | Priority | Stack (words) | Period / Trigger | Description | Blocks On |
|---|---|---|---|---|---|
| `WatchdogTask` | `osPriorityRealtime` (7) | 128 | 25 ms | Checks task health bitmask; kicks IWDG if all bits set | Task health bitmask poll |
| `SafetyCommTask` | `osPriorityRealtime` (7) | 512 | 10 ms | Initiates SPI frame exchange with Safety MCU; processes permissive response | `xSPIMutex` |
| `CANRxTask` | `osPriorityHigh+1` (6) | 512 | Event-driven | Dequeues CAN FD frames from FDCAN Rx FIFO; dispatches to consumers | FDCAN Rx FIFO ISR notification |
| `TrafficEngineTask` | `osPriorityHigh` (5) | 1024 | 50 ms | Phase state machine; generates `phase_request_t`; awaits safety permissive | `xPhaseCommandQueue` + safety permissive event |
| `PowerMonitorTask` | `osPriorityNormal` (4) | 256 | 1 s | Monitors battery ADC and voltage divider; sets/clears `SYSEVT_BATTERY_OK` | CAN Rx callback via `xCANRxNotify` |
| `TelemetryTask` | `osPriorityNormal` (4) | 2048 | 30 s (configurable) | Publishes heartbeat and status to MQTT broker over LTE | Network connectivity event |
| `CommissioningTask` | `osPriorityNormal-1` (3) | 1024 | Event-driven | Services USART3 command interface; parses configuration commands | `xUSART3RxQueue` |
| `FaultLogTask` | `osPriorityLow+1` (2) | 512 | Event-driven | Dequeues fault events; serialises to FRAM ring buffer | `xFaultQueue` |

### 3.2 Priority Rationale

- `WatchdogTask` and `SafetyCommTask` share the highest priority to ensure the IWDG is kicked and the safety heartbeat is maintained before any other work is performed. `WatchdogTask` runs only 25 ms intervals and blocks immediately after checking the health bitmask; it does not consume CPU while other real-time work is pending.
- `CANRxTask` is assigned `osPriorityHigh+1` rather than Realtime to avoid starving the safety SPI exchange in the event of a CAN flood. CAN frame processing latency up to one `SafetyCommTask` period (10 ms) is acceptable.
- `TrafficEngineTask` at `osPriorityHigh` ensures phase decisions are never delayed by telemetry or logging activity.
- `TelemetryTask` is allocated a 2048-word stack to accommodate the Quectel AT command parser and MQTT client state machine, which have significant stack depth.

### 3.3 Stack Overflow Detection

FreeRTOS stack overflow detection is enabled with `configCHECK_FOR_STACK_OVERFLOW = 2`. The `vApplicationStackOverflowHook` implementation asserts a `FAULT_STACK_OVERFLOW` fault log entry and then enters the fault lockout state (halting all non-critical tasks). The IWDG will fire within 50 ms if the Safety MCU SPI heartbeat is not maintained after a stack overflow.

---

## 4. Inter-Task Communication

### 4.1 Queues

| Queue Handle | Element Type | Depth | Producers | Consumers |
|---|---|---|---|---|
| `xPhaseCommandQueue` | `phase_request_t` (8 B) | 4 | `TrafficEngineTask`, `CommissioningTask` (manual override) | `SafetyCommTask` (forwards to Safety MCU) |
| `xFaultQueue` | `fault_log_entry_t` (32 B) | 16 | Any task via `FAULT_LOG()` macro | `FaultLogTask` |
| `xTelemetryQueue` | `telemetry_packet_t` (128 B) | 2 | `TrafficEngineTask`, `PowerMonitorTask` | `TelemetryTask` |
| `xUSART3RxQueue` | `uint8_t` (1 B) | 256 | USART3 Rx ISR | `CommissioningTask` |

The `xPhaseCommandQueue` depth of 4 is intentional. If the Safety MCU is inhibited and cannot accept phase requests, the queue will fill. `TrafficEngineTask` checks queue fullness on each cycle and enters `FAULT_LOCKOUT` if the queue remains full for more than 500 ms, indicating a safety communication failure.

### 4.2 Event Groups

All system-wide state signals are carried on `xSystemEventGroup` to allow any task to block on multiple events simultaneously using `xEventGroupWaitBits()`.

| Bit Symbol | Bit Position | Set By | Cleared By | Meaning |
|---|---|---|---|---|
| `SYSEVT_SAFETY_READY` | 0 | `SafetyCommTask` | `SafetyCommTask` | Safety MCU reports operational, inhibit released |
| `SYSEVT_CAN_HEALTHY` | 1 | `CANRxTask` | `CANRxTask` | CAN FD bus receiving valid heartbeats |
| `SYSEVT_BATTERY_OK` | 2 | `PowerMonitorTask` | `PowerMonitorTask` | Battery voltage above operational threshold |
| `SYSEVT_PHASE_ACTIVE` | 3 | `TrafficEngineTask` | `TrafficEngineTask` | A non-all-red phase is currently active |
| `SYSEVT_FAULT_ACTIVE` | 4 | Any task | `CommissioningTask` (on fault clear command) | An active fault condition is present |
| `SYSEVT_BATTERY_LOW` | 5 | `PowerMonitorTask` | `PowerMonitorTask` | Battery below low-battery warning threshold |
| `SYSEVT_RADIO_CONNECTED` | 6 | `TelemetryTask` | `TelemetryTask` | LTE bearer is established and MQTT connected |

### 4.3 Mutexes

| Mutex Handle | Shared Resource | Typical Holders | Max Hold Time |
|---|---|---|---|
| `xSPIMutex` | SPI2 bus (shared between FRAM and Safety MCU) | `SafetyCommTask`, `FaultLogTask` | 2 ms (FRAM page write) |
| `xUARTMutex` | USART3 transmit path (service port) | `CommissioningTask`, `TelemetryTask` | 100 ms |
| `xFRAMMutex` | FRAM logical address space | `FaultLogTask`, `CommissioningTask` | 5 ms |

`xSPIMutex` and `xFRAMMutex` are separate objects because FRAM access and Safety MCU SPI exchange must not be conflated: the FRAM CS line and Safety MCU CS line are distinct GPIOs, but they share the same SPI bus clock and data lines. Holding `xSPIMutex` blocks both FRAM and Safety MCU access; `xFRAMMutex` is an additional logical guard that prevents concurrent FRAM address pointer manipulation between `FaultLogTask` and `CommissioningTask`.

### 4.4 Semaphores

| Semaphore Handle | Type | Given By | Taken By | Purpose |
|---|---|---|---|---|
| `xCANTxSemaphore` | Binary | FDCAN Tx complete ISR | `CANRxTask` (before enqueuing Tx frames) | Signals CAN Tx mailbox availability |

---

## 5. Traffic Engine State Machine

### 5.1 State Diagram

The Traffic Engine runs as a deterministic finite state machine inside `TrafficEngineTask`. All state transitions produce a phase command sent to the Safety MCU; the Safety MCU does not transition unless its own intergreen and conflict checks pass.

```
                     power-on / reset
                            |
                            v
                       ┌─────────┐
                       │ STARTUP │
                       └────┬────┘
                            │
              ┌─────────────┴───────────────┐
              │                             │
        all systems ready             timeout / fault
        (SAFETY_READY + CAN_HEALTHY)        │
              │                             │
              v                             v
          ┌─────────┐               ┌───────────────┐
          │ ALL_RED │◄──────────────│ FAULT_LOCKOUT │
          └────┬────┘  fault cleared└───────────────┘
               │         (by commissioning            ^
               │          command or auto-clear)      │
        phase selected                                │
        (from plan or                          fault detected
         manual request)                      (any task)
               │                                      │
               v                                      │
        ┌──────────────┐                              │
        │ PHASE_ACTIVE ├──────────────────────────────┘
        └──────┬───────┘
               │
         phase complete
         (timer expiry or
          plan advance)
               │
               v
       ┌──────────────────┐
       │ AMBER_CLEARANCE  │
       └────────┬─────────┘
                │
          amber timer expired
          (minimum 3 s, TOPAS)
                │
                v
       ┌────────────────────┐
       │ ALL_RED_CLEARANCE  │
       └────────┬───────────┘
                │
          clearance complete
          (minimum 2 s all-red)
                │
                v
           ┌─────────┐
           │ ALL_RED │ (next phase selected)
           └─────────┘


  Manual override path:
  ─────────────────────
  ANY STATE ──[override command]──► MANUAL_OVERRIDE
  MANUAL_OVERRIDE ──[override released]──► ALL_RED_CLEARANCE

  Blackout path:
  ──────────────
  ANY STATE ──[blackout command]──► BLACKOUT
  BLACKOUT ──[blackout released]──► STARTUP
```

### 5.2 State Descriptions

| State | Phase Outputs | Entry Action | Exit Condition |
|---|---|---|---|
| `STARTUP` | All inhibited | Assert `PHASE_CMD_ALL_INHIBIT` to Safety MCU; wait for `SYSEVT_SAFETY_READY` | All systems ready, or 5 s timeout → `FAULT_LOCKOUT` |
| `ALL_RED` | All red aspects | Send `PHASE_CMD_ALL_RED`; start next-phase timer | Phase selection timer expires; plan advance command; or manual request |
| `PHASE_ACTIVE` | Selected phase green + opposing reds | Send `PHASE_CMD_SET_PHASE(n)` to Safety MCU; start phase duration timer | Phase timer expires; demand satisfied; or fault event |
| `AMBER_CLEARANCE` | Active phase amber + opposing reds | Send `PHASE_CMD_AMBER(n)`; start 3 s amber timer | Amber timer expires (minimum 3 s) |
| `ALL_RED_CLEARANCE` | All red aspects | Send `PHASE_CMD_ALL_RED`; start 2 s clearance timer | Clearance timer expires |
| `FAULT_LOCKOUT` | All inhibited | Log fault; assert inhibit request to Safety MCU; set `SYSEVT_FAULT_ACTIVE` | `FAULT_CLEAR` commissioning command received and fault condition resolved |
| `MANUAL_OVERRIDE` | Operator-selected | Honour override phase command from commissioning interface | Override released via commissioning command |
| `BLACKOUT` | All outputs off | Send `PHASE_CMD_BLACKOUT`; de-energise all channels | Blackout released via commissioning command |

### 5.3 Plan Execution

In normal timed operation, the Traffic Engine cycles through a phase plan stored in FRAM phase configuration. Each plan entry specifies a phase index and a minimum green duration. The `TrafficEngineTask` advances through the plan on each `ALL_RED` state entry. Vehicle detection demand (via CAN from detector loops) can extend the green period up to the configured maximum green, but cannot override the intergreen timing.

---

## 6. Safety Supervisor Protocol (SPI)

### 6.1 Physical Layer

The SPI bus between the Application MCU (master) and Safety MCU (slave) uses the following configuration:

| Parameter | Value |
|---|---|
| Interface | SPI2 (Application MCU), SPI1 (Safety MCU) |
| Mode | Full-duplex, CPOL=0, CPHA=0 |
| Clock speed | 8 MHz |
| Frame size | 8-bit |
| CS polarity | Active low, software managed |
| Cable length | Board-internal (< 10 cm) |

### 6.2 Frame Format

Every SPI exchange consists of a simultaneous 16-byte transmit (Application MCU → Safety MCU) and 16-byte receive (Safety MCU → Application MCU). The master initiates every 10 ms.

**Command Frame (Application MCU → Safety MCU):**

| Byte | Field | Value / Description |
|---|---|---|
| 0 | SOF | `0xA5` (fixed start-of-frame marker) |
| 1 | OPCODE | Command opcode (see table below) |
| 2–13 | PAYLOAD | 12 bytes of opcode-specific data |
| 14 | CRC8 | CRC-8/MAXIM over bytes 0–13 |
| 15 | EOF | `0x5A` (fixed end-of-frame marker) |

**Status Frame (Safety MCU → Application MCU):**

| Byte | Field | Value / Description |
|---|---|---|
| 0 | SOF | `0xA5` |
| 1 | STATUS | Safety MCU status byte (see below) |
| 2–13 | PAYLOAD | 12 bytes — current permissive state, output states, fault flags |
| 14 | CRC8 | CRC-8/MAXIM over bytes 0–13 |
| 15 | EOF | `0x5A` |

**Command Opcodes:**

| Opcode | Symbol | Description |
|---|---|---|
| `0x01` | `CMD_HEARTBEAT` | Keepalive; no phase change |
| `0x02` | `CMD_PHASE_REQUEST` | Request phase transition; phase index in PAYLOAD[0] |
| `0x03` | `CMD_ALL_RED` | Request all-red state |
| `0x04` | `CMD_INHIBIT_ALL` | Request output inhibit (all outputs off) |
| `0x05` | `CMD_BLACKOUT` | Request blackout mode |
| `0x06` | `CMD_FAULT_RESET` | Request fault condition reset (only honoured if fault condition cleared) |

**Safety MCU Status Byte (STATUS field):**

| Bit | Symbol | Meaning when set |
|---|---|---|
| 7 | `STAT_PERMISSIVE` | Output permissive is currently granted |
| 6 | `STAT_INHIBIT_ACTIVE` | Hardware inhibit is asserted (outputs off) |
| 5 | `STAT_CONFLICT_DETECTED` | Conflict matrix violation detected |
| 4 | `STAT_INTERGREEN_ACTIVE` | Intergreen timer is running |
| 3 | `STAT_FAULT` | Safety MCU has an active fault |
| 2 | `STAT_WATCHDOG_WARNING` | Safety MCU IWDG approaching expiry |
| 1 | `STAT_PHASE_ACCEPTED` | Last phase request accepted |
| 0 | `STAT_PHASE_REJECTED` | Last phase request rejected (conflict or intergreen) |

### 6.3 Frame Validation and Timeout Behaviour

The Safety MCU validates each received frame as follows:

1. Check SOF byte equals `0xA5`.
2. Check EOF byte equals `0x5A`.
3. Compute CRC-8/MAXIM over bytes 0–13; compare with byte 14.
4. If all checks pass, process opcode.

If **3 consecutive frames fail validation** (any check), the Safety MCU immediately de-asserts the inhibit GPIO. This is a hardware GPIO transition — it is not communicated back over SPI. The de-assertion occurs within the Safety MCU superloop cycle time (< 1 ms from the third invalid frame).

The inhibit GPIO is also de-asserted if the Safety MCU receives no SPI chip-select edge for more than **30 ms** (3 missed 10 ms cycles). This timeout is implemented using a hardware timer (TIM1) on the Safety MCU, with the CS GPIO triggering a timer reset on each falling edge.

### 6.4 CAN FD Health and Permissive Evaluation

The CAN FD bus health (reported in the Safety MCU status frame PAYLOAD) contributes to the Safety MCU's permissive evaluation. If the Safety MCU has not received a valid LSO-100 heartbeat for more than 300 ms, it flags a CAN health fault in its status frame. This causes `SafetyCommTask` to set `SYSEVT_FAULT_ACTIVE` and command `ALL_RED`. However, CAN health is **not** part of the hardware inhibit decision path — the hardware inhibit line is solely controlled by the SPI frame validity and timeout logic described above.

---

## 7. CAN FD Bus Protocol

### 7.1 Physical Topology

```
                          LCU-100 (Node 0x01x)
                               │
              ┌────────────────┼──────────────────┐
              │                │                  │
         LSO-100          LSO-100            LPB-100
        Approach A        Approach B         Push Button
        (Node 0x02A)      (Node 0x02B)       (Node 0x03x)
              │
         LPI-100 (Phase 2)
        Pedestrian Indicator
        (Node 0x04x)
```

The LCU-100 sits at the centre of a star topology. Each branch runs 120 Ω termination resistors at the far end. The LCU-100 end of each branch is terminated by a 240 Ω split termination (2 × 120 Ω in series with a 100 nF capacitor to ground) to reduce high-frequency emission.

### 7.2 Bit Rate Configuration

| Parameter | Value |
|---|---|
| Nominal bit rate | 1 Mbps |
| Data phase bit rate | 4 Mbps (CAN FD with BRS) |
| Sample point (nominal) | 80% |
| Sample point (data) | 75% |
| Transceiver | TJA1044GT (HSE-rated, galvanically isolated) |
| Maximum cable length per branch | 30 m |

### 7.3 Message Cycle Times

| Message Type | Nominal Period | Priority | DLC (bytes) | Notes |
|---|---|---|---|---|
| Node Heartbeat | 100 ms | High | 8 | All nodes; loss within 300 ms triggers fault |
| Phase Command | 200 ms | High | 16 (FD frame) | LCU → LSO; carries phase index + intergreen state |
| Fault Report | Immediate (event-driven) | Urgent | 16 (FD frame) | Any node; triggers fault log on LCU |
| Telemetry | 500 ms | Normal | 64 (FD frame) | LSO → LCU; voltage, current, temperature |
| Battery Status | 1000 ms | Normal | 8 | LPB-100 → LCU; battery voltage and SoC |

### 7.4 Node ID Allocation

| Node Type | ID Range | Extended (29-bit) ID Mask |
|---|---|---|
| LCU-100 (this unit) | `0x010` – `0x01F` | `0x00000010` |
| LSO-100 (Signal Output modules) | `0x020` – `0x02F` | `0x00000020` |
| LPB-100 (Push Button modules) | `0x030` – `0x03F` | `0x00000030` |
| LPI-100 (Pedestrian Indicator) | `0x040` – `0x04F` | `0x00000040` |
| Safety MCU (internal CAN node) | `0x050` – `0x05F` | `0x00000050` |

The upper nibble of the node ID encodes the device type. The lower nibble encodes the unit address (0–15), allowing up to 16 nodes of each type on a single bus. Current designs use a maximum of 4 LSO-100 nodes (one per approach).

### 7.5 Error Handling and Recovery

**Bus-off recovery:** The FDCAN peripheral is configured for automatic bus-off recovery. After the bus-off condition is detected (transmit error counter > 255), the FDCAN peripheral waits for 128 occurrences of 11 consecutive recessive bits before re-entering the error-active state. This is the standard ISO 11898-1 bus-off recovery sequence.

**LSO heartbeat loss:** If the `CANRxTask` does not receive a heartbeat from any configured LSO-100 node within 300 ms:
1. `CANRxTask` clears `SYSEVT_CAN_HEALTHY`.
2. `TrafficEngineTask` detects the event group change and transitions to `FAULT_LOCKOUT`.
3. A `FAULT_CAN_HEARTBEAT_LOSS` entry is written to the fault log.
4. `SafetyCommTask` sends `CMD_INHIBIT_ALL` to the Safety MCU on the next 10 ms cycle.

**Rx FIFO overrun:** If the FDCAN Rx FIFO overflows (CANRxTask not servicing fast enough), the FDCAN peripheral raises an overrun interrupt. The ISR logs a `FAULT_CAN_RX_OVERRUN` event and clears the FIFO. Non-critical telemetry messages are dropped silently; heartbeat messages are never dropped because the heartbeat CAN ID is mapped to FIFO0 with the highest hardware filter priority.

---

## 8. Secure Boot and Firmware Update

### 8.1 Bootloader

The bootloader resides at `0x08000000` and is the first code executed after reset. It is a minimal, standalone binary with no dependence on the application firmware libraries. The bootloader is write-protected using the STM32H743 Flash write protection register (FLASH_WRP1xR) and is never updated OTA.

Bootloader sequence:

1. Configure clocks to internal RC oscillator (HSI, 64 MHz) — do not enable PLL at this stage.
2. Check Backup SRAM for a valid OTA update flag.
3. If OTA flag set: validate Slot B image header and CRC32; if valid, copy Slot B to Slot A and clear OTA flag; if invalid, clear OTA flag and continue with Slot A.
4. Validate Slot A image header (magic number `0x4C554D49`, CRC32 over image body).
5. If Slot A valid: increment boot attempt counter in Backup SRAM; launch Slot A.
6. If Slot A invalid, or boot attempt counter ≥ 3: clear boot attempt counter; validate Slot B; launch Slot B (safe factory fallback).
7. If both slots invalid: hang with IWDG disabled (unit requires field service).

### 8.2 Image Header Format

Each application image begins with a 64-byte header at the base of its flash slot:

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 4 B | Magic | `0x4C554D49` ("LUMI") |
| 4 | 4 B | Version | BCD-encoded: `0xMMmmPPBB` (major, minor, patch, build) |
| 8 | 4 B | Image size | Size of application body in bytes (excluding header) |
| 12 | 4 B | CRC32 | CRC-32/ISO-HDLC over application body |
| 16 | 4 B | Flags | Bit 0: `FLAG_OTA_COMMITTED`; Bit 1: `FLAG_SAFE_FALLBACK` |
| 20 | 44 B | Reserved | Padded to 64 B; must be `0xFF` |

### 8.3 OTA Update Process

OTA firmware delivery is performed over the LTE link by `TelemetryTask`. The update process is:

1. MQTT broker delivers an `ota_manifest` message to the `lcu/{serial}/ota/manifest` topic containing the target version, image size, CRC32, and HTTPS URL.
2. `TelemetryTask` validates the manifest signature (HMAC-SHA256 with a device-unique key stored in Option Bytes).
3. Image is downloaded in 4 KB chunks to a RAM_D2 receive buffer, then written to Flash Slot B using HAL Flash programming functions.
4. After all chunks received, CRC32 of the complete Slot B image body is verified against the manifest.
5. On verification success, the OTA update flag is written to Backup SRAM and a controlled system reset is triggered.
6. The bootloader handles the rest (see Section 8.1).
7. On first successful run of the new image (measured by the application running for > 60 s without a watchdog reset), `TelemetryTask` publishes an `ota_complete` acknowledgement and clears the boot attempt counter.

### 8.4 Read-Out Protection

RDP Level 1 is programmed in production. This disables JTAG Flash readback, preventing firmware extraction from units in the field. Debug connections via SWD remain available to Lumina engineering tools using an authenticated unlock sequence (device-unique key). RDP Level 2 (which permanently disables all debug access) is not used, to preserve the ability for field service and firmware recovery.

---

## 9. RTOS Watchdog Architecture

### 9.1 Hardware IWDG (Application MCU)

The STM32H743 Independent Watchdog (IWDG) is configured with a **50 ms window**. The IWDG is clocked by the LSI RC oscillator (32 kHz nominal, independent of all PLL and HSE paths). It is configured as a windowed watchdog: the IWDG will fire if the key register is written before 25 ms (early kick) or after 50 ms (late kick). This prevents a runaway or looping task from continuously kicking the watchdog at the wrong time.

The IWDG is kicked **exclusively from `WatchdogTask`**. No other task touches the IWDG key register.

### 9.2 Task Health Bitmask

Each FreeRTOS task maintains one bit in a shared `uint32_t g_task_health_mask` variable (declared `volatile`). On each execution cycle, a task sets its assigned bit using an atomic bitwise OR. `WatchdogTask` reads the bitmask on each 25 ms cycle and clears it after checking. If all expected bits are set, `WatchdogTask` kicks the IWDG and clears the mask. If any bit is missing for **2 consecutive WatchdogTask cycles** (50 ms), `WatchdogTask` stops kicking the IWDG, which fires after the 50 ms window expires, triggering a system reset.

| Task | Health Bit | Bit Position |
|---|---|---|
| `SafetyCommTask` | `HEALTH_SAFETY_COMM` | Bit 0 |
| `CANRxTask` | `HEALTH_CAN_RX` | Bit 1 |
| `TrafficEngineTask` | `HEALTH_TRAFFIC_ENGINE` | Bit 2 |
| `FaultLogTask` | `HEALTH_FAULT_LOG` | Bit 3 |
| `TelemetryTask` | `HEALTH_TELEMETRY` | Bit 4 |
| `CommissioningTask` | `HEALTH_COMMISSIONING` | Bit 5 |
| `PowerMonitorTask` | `HEALTH_POWER_MONITOR` | Bit 6 |

`WatchdogTask` itself does not have a health bit (it is the watchdog). Its liveness is demonstrated by the fact that the IWDG continues to be kicked.

**Important:** `TelemetryTask` and `CommissioningTask` health bits have an extended grace period mechanism. These tasks can legitimately block for longer periods (e.g., waiting for an LTE attach or a UART receive). Their health bits use a separate 5-cycle (125 ms) timeout rather than the default 2-cycle timeout. If these tasks do not update within 125 ms, `WatchdogTask` logs a warning but does not stop kicking the IWDG immediately; a 30 s grace timer is started. This prevents a temporary LTE modem timeout from causing a system reset during normal field operation.

### 9.3 Safety MCU Watchdog Architecture

The Safety MCU (STM32G071) has its own completely independent IWDG, also configured for a **50 ms window**, driven by the STM32G071 internal RC oscillator. This watchdog is kicked within the Safety MCU superloop at every iteration.

In addition to the hardware IWDG, the Safety MCU outputs a **1 Hz software watchdog toggle** on a dedicated GPIO. This GPIO drives a timer capture input (TIM5_CH1) on the Application MCU. `SafetyCommTask` monitors the capture register and reports a `STAT_WATCHDOG_WARNING` condition if the toggle is not observed within 1.5 s. Loss of the 1 Hz toggle for more than 2.5 s causes `SafetyCommTask` to log `FAULT_SAFETY_MCU_WATCHDOG_LOST` and transition the Traffic Engine to `FAULT_LOCKOUT`.

---

## 10. Logging and Diagnostics

### 10.1 FRAM Fault Log Ring Buffer

The fault log is stored in the FRAM as a ring buffer. The ring buffer occupies the full 512 KB FRAM address space (excluding the upper 8 KB reserved for phase configuration and calibration).

| Parameter | Value |
|---|---|
| Entry size | 32 bytes |
| Total capacity | 16,000 entries (512 KB ÷ 32 B) |
| Retention | Non-volatile (FRAM retains without power, > 10 years) |
| Write endurance | 10^13 cycles per cell (effectively unlimited) |

### 10.2 Fault Log Entry Format

Each 32-byte entry has the following structure:

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 4 B | Sequence number | Monotonically increasing, persistent across resets |
| 4 | 4 B | RTC timestamp | Unix epoch, from STM32H743 RTC (BCD calendar) |
| 8 | 2 B | Fault code | See fault code enumeration in `fault_codes.h` |
| 10 | 1 B | Severity | `SEV_INFO`, `SEV_WARNING`, `SEV_MAJOR`, `SEV_CRITICAL` |
| 11 | 1 B | Source board | Node ID of reporting board |
| 12 | 1 B | Phase index | Phase active at time of fault (0xFF if none) |
| 13 | 3 B | Reserved | Padded to alignment |
| 16 | 16 B | Extra data | Fault-specific payload (e.g., ADC reading, CAN frame ID) |

### 10.3 Log Access via Service UART

The commissioning interface (USART3, 115200 baud, 8N1) provides a `faults` command that dumps the fault log in human-readable ASCII format. The output includes column headers and one row per entry. The `faults clear` command erases all entries (sets all bytes to `0xFF`). Both commands require PIN authentication.

Example output:

```
SEQ       TIMESTAMP            CODE     SEV       BOARD  PHASE  EXTRA
00012345  2026-06-18T09:14:22  0x0042   MAJOR     0x01   A      V=11.8V
00012346  2026-06-18T09:15:03  0x0010   CRITICAL  0x01   --     --
```

### 10.4 Cloud Fault Synchronisation

`TelemetryTask` subscribes to `xFaultQueue` notifications. On receipt of a fault event with severity `SEV_MAJOR` or above, the task attempts to publish the fault entry as a JSON payload to the MQTT topic `lcu/{serial}/faults` within **5 seconds** of the fault occurring. QoS 1 is used for fault events (at-least-once delivery). If the LTE bearer is not available, the publish is queued in the `xTelemetryQueue` and retried when connectivity is restored.

---

## 11. Non-Safety Communications Architecture

### 11.1 LTE (Quectel EC21)

The Quectel EC21 LTE Cat-1 modem connects to the Application MCU via USART3 (AT command interface, 115200 baud). Power control is via a dedicated GPIO (`LTE_PWRKEY`). The modem is managed by a lightweight AT command state machine within `TelemetryTask`.

| Parameter | Value |
|---|---|
| Interface | USART3, 115200 baud, 8N1, hardware flow control (RTS/CTS) |
| Protocol | TCP/IP, MQTT v3.1.1 |
| MQTT broker | Configurable via commissioning interface |
| MQTT QoS | QoS 0 for telemetry; QoS 1 for faults and OTA |
| Antenna | External SMA, 698–2700 MHz |

### 11.2 MQTT Topic Structure

| Topic | Direction | QoS | Content |
|---|---|---|---|
| `lcu/{serial}/telemetry` | LCU → Broker | 0 | Periodic status JSON (voltage, phase, uptime) |
| `lcu/{serial}/faults` | LCU → Broker | 1 | Fault log entry JSON |
| `lcu/{serial}/ota/manifest` | Broker → LCU | 1 | OTA manifest (see Section 8.3) |
| `lcu/{serial}/config` | Broker → LCU | 1 | Remote configuration push |
| `lcu/{serial}/status/request` | Broker → LCU | 0 | On-demand status request |

### 11.3 GNSS (u-blox SAM-M10Q)

The u-blox SAM-M10Q GNSS receiver connects to the Application MCU via UART4 (9600 baud, NMEA protocol). Only `$GPRMC` sentences are parsed; other NMEA sentence types are discarded. The RMC sentence provides time, date, latitude, longitude, and fix validity. The GNSS timestamp is used to set the STM32H743 RTC on power-up and periodically thereafter (if fix is valid). Position is included in telemetry payloads for asset tracking.

| Parameter | Value |
|---|---|
| Interface | UART4, 9600 baud, 8N1 |
| Protocol | NMEA 0183, `$GPRMC` only |
| Update rate | 1 Hz |
| Antenna | Active patch, SMA connector |

### 11.4 Radio Synchronisation (Phase 2 Feature)

The M.2 expansion slot (M key, PCIe ×1 pinout) is reserved for a Phase 2 LoRa/UHF radio synchronisation module. The interface uses UART5 (115200 baud) for data and three GPIOs for reset, busy, and interrupt. The `SafetyCommTask` currently stubs the radio synchronisation permissive as always-granted. Phase 2 implementation will add a `RadioSyncTask` and a corresponding safety analysis for multi-controller coordination.

---

*End of Document LCU-100-FW-ARCH-001 Rev 1.0*

*This document is Lumina Engineering confidential. Do not distribute outside Lumina Engineering without written authorisation.*
