/**
 * @file    fault_codes.h
 * @brief   System-wide fault code definitions for the Lumina LCU-100 platform.
 *
 * Fault code layout:
 *   Bits [15:8]  Category byte  (0x01–0x07)
 *   Bits  [7:0]  Code within category (0x00–0xFF)
 *
 * Category map:
 *   0x01xx  MCU faults              — microcontroller-level failures
 *   0x02xx  Safety faults           — safety supervisor violations
 *   0x03xx  Output faults           — LSO-100 lamp/load channel faults
 *   0x04xx  Power faults            — LPB-100 power supply faults
 *   0x05xx  Communications faults   — CAN FD / RS485 / SPI faults
 *   0x06xx  Configuration faults    — plan / NVM / parameter errors
 *   0x07xx  Hardware faults         — PCB, sensor, peripheral failures
 *
 * Copyright (c) Lumina Traffic Systems Ltd.  All rights reserved.
 * SPDX-License-Identifier: Proprietary
 */

#ifndef FAULT_CODES_H
#define FAULT_CODES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Fault severity levels
 * ========================================================================= */

typedef enum
{
    /** Informational event logged only; no operational impact. */
    SEVERITY_INFO       = 0U,

    /** Degraded condition; system continues but operator should inspect. */
    SEVERITY_WARNING    = 1U,

    /** Serious fault; phase operation inhibited until resolved. */
    SEVERITY_CRITICAL   = 2U,

    /** Safety-critical fault; immediate all-red lockout, hardware inhibit. */
    SEVERITY_SAFETY     = 3U,
} fault_severity_t;

/* =========================================================================
 * Fault action codes
 * ========================================================================= */

typedef enum
{
    /** Record in event log only; no operational change. */
    ACTION_LOG_ONLY         = 0U,

    /** Activate audible/visual alarm; continue operation. */
    ACTION_ALARM            = 1U,

    /** Inhibit affected output channel(s); keep other phases running. */
    ACTION_INHIBIT_OUTPUT   = 2U,

    /** Transition to all-red, inhibit all outputs, latch fault lockout. */
    ACTION_SAFE_SHUTDOWN    = 3U,
} fault_action_t;

/* =========================================================================
 * Fault code definitions
 * ========================================================================= */

/** No fault active. */
#define FAULT_NONE                              0x0000U

/* ---- Category 0x01: MCU faults ----------------------------------------- */

/** Watchdog timer expired without being petted; MCU reset occurred. */
#define FAULT_MCU_WATCHDOG_EXPIRED              0x0101U

/** Stack overflow detected in any RTOS task. */
#define FAULT_MCU_STACK_OVERFLOW                0x0102U

/** Runtime flash CRC check failed; firmware integrity not confirmed. */
#define FAULT_MCU_FLASH_CRC_ERROR               0x0103U

/** SRAM bitflip detected by scrubbing routine (ECC or walk-pattern). */
#define FAULT_MCU_RAM_ECC_ERROR                 0x0104U

/** CPU die temperature exceeded 110 °C; thermal runaway risk. */
#define FAULT_MCU_OVERTEMPERATURE               0x0105U

/** Internal oscillator / PLL lock failure; clock integrity suspect. */
#define FAULT_MCU_CLOCK_FAULT                   0x0106U

/** DMA transfer error on a critical peripheral (SPI, CAN, UART). */
#define FAULT_MCU_DMA_ERROR                     0x0107U

/** RTOS task has exceeded its scheduling deadline. */
#define FAULT_MCU_TASK_DEADLINE_MISS            0x0108U

/** NVM (internal flash) write or erase operation failed. */
#define FAULT_MCU_NVM_WRITE_FAIL                0x0109U

/** Internal ADC reference voltage out of tolerance. */
#define FAULT_MCU_ADC_REFERENCE_FAULT           0x010AU

/* ---- Category 0x02: Safety faults --------------------------------------- */

/** Phase activation would create a conflicting concurrent green display. */
#define FAULT_SAFETY_CONFLICT_DETECTED          0x0201U

/** Safety supervisor SPI watchdog expired; link to STM32G071 lost. */
#define FAULT_SAFETY_SUPERVISOR_WDG_EXPIRED     0x0202U

/** Supervisor's output inhibit relay failed to assert when commanded. */
#define FAULT_SAFETY_INHIBIT_RELAY_FAIL         0x0203U

/** Conflict matrix stored in supervisor NVM failed CRC validation. */
#define FAULT_SAFETY_CONFLICT_MATRIX_CRC        0x0204U

/** Intergreen timer elapsed faster than allowed by hardware timer. */
#define FAULT_SAFETY_INTERGREEN_TIMER_FAULT     0x0205U

/** Supervisor reported a self-test failure during startup sequence. */
#define FAULT_SAFETY_SELF_TEST_FAIL             0x0206U

/** Safety supervisor firmware version mismatch with main MCU protocol. */
#define FAULT_SAFETY_VERSION_MISMATCH           0x0207U

/** Red channel was not confirmed on during all-red transition. */
#define FAULT_SAFETY_RED_CONFIRM_TIMEOUT        0x0208U

/** Supervisor hardware inhibit line is stuck de-asserted. */
#define FAULT_SAFETY_INHIBIT_LINE_STUCK         0x0209U

/** Phase dwell time exceeded the plan maximum; forced termination. */
#define FAULT_SAFETY_PHASE_TIMEOUT              0x020AU

/* ---- Category 0x03: Output faults (LSO-100) ----------------------------- */

/** Open circuit detected on lamp channel 1 (PROFET IS feedback = 0). */
#define FAULT_OUTPUT_OPEN_CIRCUIT_CH1           0x0301U

/** Open circuit detected on lamp channel 2. */
#define FAULT_OUTPUT_OPEN_CIRCUIT_CH2           0x0302U

/** Open circuit detected on lamp channel 3. */
#define FAULT_OUTPUT_OPEN_CIRCUIT_CH3           0x0303U

/** Open circuit detected on lamp channel 4. */
#define FAULT_OUTPUT_OPEN_CIRCUIT_CH4           0x0304U

/** Open circuit detected on lamp channel 5. */
#define FAULT_OUTPUT_OPEN_CIRCUIT_CH5           0x0305U

/** Open circuit detected on lamp channel 6. */
#define FAULT_OUTPUT_OPEN_CIRCUIT_CH6           0x0306U

/** Open circuit detected on lamp channel 7. */
#define FAULT_OUTPUT_OPEN_CIRCUIT_CH7           0x0307U

/** Open circuit detected on lamp channel 8. */
#define FAULT_OUTPUT_OPEN_CIRCUIT_CH8           0x0308U

/** Short circuit / overcurrent trip on lamp channel (channel in fault_data). */
#define FAULT_OUTPUT_SHORT_CIRCUIT              0x0309U

/** PROFET device over-temperature thermal shutdown (channel in fault_data). */
#define FAULT_OUTPUT_PROFET_THERMAL             0x030AU

/** Load current exceeds expected range; lamp wattage suspect. */
#define FAULT_OUTPUT_LOAD_CURRENT_HIGH          0x030BU

/** Load current below expected range; partial lamp failure or open. */
#define FAULT_OUTPUT_LOAD_CURRENT_LOW           0x030CU

/** Output state does not match commanded state after settling time. */
#define FAULT_OUTPUT_STATE_MISMATCH             0x030DU

/** LSO board supply rail out of tolerance; all output quality suspect. */
#define FAULT_OUTPUT_SUPPLY_FAULT               0x030EU

/* ---- Category 0x04: Power faults (LPB-100) ------------------------------ */

/** Battery state of charge has reached the critical shutdown threshold. */
#define FAULT_POWER_BATTERY_CRITICAL            0x0401U

/** Battery state of charge is low; reduced runtime remaining. */
#define FAULT_POWER_BATTERY_LOW                 0x0402U

/** DC bus voltage exceeds over-voltage trip threshold. */
#define FAULT_POWER_BUS_OVER_VOLTAGE            0x0403U

/** DC bus voltage below under-voltage lockout threshold. */
#define FAULT_POWER_BUS_UNDER_VOLTAGE           0x0404U

/** Mains AC input has been lost; operating on battery. */
#define FAULT_POWER_MAINS_LOST                  0x0405U

/** Battery is not connected or has been removed. */
#define FAULT_POWER_BATTERY_NOT_PRESENT         0x0406U

/** Battery temperature sensor reading out of range or open circuit. */
#define FAULT_POWER_BATTERY_TEMP_FAULT          0x0407U

/** Battery cell over-temperature fault; charging suspended. */
#define FAULT_POWER_BATTERY_OVERTEMP            0x0408U

/** Charger PWM controller fault; charging capability lost. */
#define FAULT_POWER_CHARGER_FAULT               0x0409U

/** LPB internal 5 V regulation fault; sensor data unreliable. */
#define FAULT_POWER_5V_RAIL_FAULT               0x040AU

/* ---- Category 0x05: Communications faults ------------------------------- */

/** CAN FD link to LSO-100 signal output board lost (heartbeat timeout). */
#define FAULT_COMMS_CAN_LSO_LINK_LOSS           0x0501U

/** CAN FD link to LPB-100 power board lost (heartbeat timeout). */
#define FAULT_COMMS_CAN_LPB_LINK_LOSS           0x0502U

/** CAN FD link to LPI-100 pedestrian interface lost. */
#define FAULT_COMMS_CAN_LPI_LINK_LOSS           0x0503U

/** CAN FD link to safety supervisor lost (heartbeat timeout). */
#define FAULT_COMMS_CAN_SAFETY_LINK_LOSS        0x0504U

/** CAN FD bus-off condition detected; controller has withdrawn from bus. */
#define FAULT_COMMS_CAN_BUS_OFF                 0x0505U

/** CAN FD message received with CRC error (error passive threshold). */
#define FAULT_COMMS_CAN_FRAME_ERROR             0x0506U

/** RS485 uplink UART framing or parity error (remote monitoring). */
#define FAULT_COMMS_RS485_FRAME_ERROR           0x0507U

/** RS485 uplink response timeout from remote monitoring centre. */
#define FAULT_COMMS_RS485_TIMEOUT               0x0508U

/** SPI to safety supervisor: CRC error threshold exceeded. */
#define FAULT_COMMS_SPI_CRC_ERROR               0x0509U

/** SPI to safety supervisor: transfer did not complete within deadline. */
#define FAULT_COMMS_SPI_TIMEOUT                 0x050AU

/* ---- Category 0x06: Configuration faults -------------------------------- */

/** Loaded phase plan failed CRC32 validation. */
#define FAULT_CONFIG_PLAN_CRC_FAIL              0x0601U

/** Phase plan specifies an approach channel index beyond LSO capacity. */
#define FAULT_CONFIG_INVALID_CHANNEL_MAP        0x0602U

/** Phase plan has zero phases or more than MAX_PHASES. */
#define FAULT_CONFIG_PLAN_PHASE_COUNT           0x0603U

/** Phase minimum duration is greater than maximum duration. */
#define FAULT_CONFIG_PHASE_TIMER_INVALID        0x0604U

/** NVM configuration sector failed to erase or write during provisioning. */
#define FAULT_CONFIG_NVM_WRITE_FAIL             0x0605U

/** NVM configuration header magic number invalid; factory defaults used. */
#define FAULT_CONFIG_NVM_INVALID_MAGIC          0x0606U

/** Intergreen time matrix contains a value that exceeds the allowed maximum. */
#define FAULT_CONFIG_INTERGREEN_OVERFLOW        0x0607U

/** Operating mode requested is not supported by the loaded phase plan. */
#define FAULT_CONFIG_UNSUPPORTED_MODE           0x0608U

/** Approach conflict matrix is inconsistent (asymmetric or self-conflicting). */
#define FAULT_CONFIG_CONFLICT_MATRIX_INVALID    0x0609U

/** Phase plan version is incompatible with current firmware version. */
#define FAULT_CONFIG_PLAN_VERSION_MISMATCH      0x060AU

/* ---- Category 0x07: Hardware faults ------------------------------------- */

/** I2C bus to on-board RTC or EEPROM has failed. */
#define FAULT_HW_I2C_BUS_FAULT                  0x0701U

/** On-board real-time clock battery dead or RTC oscillator stopped. */
#define FAULT_HW_RTC_OSCILLATOR_FAIL            0x0702U

/** Ambient temperature sensor (NTC) open circuit or shorted. */
#define FAULT_HW_TEMP_SENSOR_FAULT              0x0703U

/** Optocoupler detector input signal quality degraded. */
#define FAULT_HW_DETECTOR_INPUT_FAULT           0x0704U

/** 24 V PROFET gate drive supply rail fault. */
#define FAULT_HW_GATE_DRIVE_SUPPLY              0x0705U

/** Hardware revision register read-back mismatch; board ID suspect. */
#define FAULT_HW_BOARD_ID_MISMATCH              0x0706U

/** External crystal oscillator has failed; using internal RC oscillator. */
#define FAULT_HW_CRYSTAL_FAIL                   0x0707U

/** Isolated DC-DC converter for CAN PHY has failed. */
#define FAULT_HW_CAN_DCDC_FAULT                 0x0708U

/** Magnetic reed switch for enclosure tamper detection opened. */
#define FAULT_HW_ENCLOSURE_TAMPER               0x0709U

/** Hardware fault latch pin driven active by an external protection IC. */
#define FAULT_HW_EXTERNAL_FAULT_LATCH           0x070AU

/* =========================================================================
 * Fault descriptor (lookup table entry)
 * ========================================================================= */

/**
 * @brief Descriptor for a single fault code entry.
 *
 * The fault table is defined in fault_codes.c and declared extern below.
 * Look up by iterating until fault_table[i].code == FAULT_NONE (sentinel).
 */
typedef struct
{
    uint16_t            code;           /**< FAULT_xxx value                         */
    fault_severity_t    severity;       /**< Severity classification                 */
    fault_action_t      action;         /**< Default corrective action               */
    const char         *description;    /**< Human-readable description string       */
} fault_descriptor_t;

/**
 * @brief Global fault descriptor lookup table.
 *
 * Terminated by an entry with code == FAULT_NONE.
 * Defined in firmware/common/config/fault_codes.c.
 */
extern const fault_descriptor_t fault_table[];

/* =========================================================================
 * Lookup helpers
 * ========================================================================= */

/**
 * @brief Look up a fault descriptor by code.
 *
 * @param code  A FAULT_xxx constant.
 * @return      Pointer to the matching fault_descriptor_t, or NULL if not found.
 */
static inline const fault_descriptor_t *fault_lookup(uint16_t code)
{
    const fault_descriptor_t *entry = fault_table;
    while (entry->code != FAULT_NONE)
    {
        if (entry->code == code) { return entry; }
        entry++;
    }
    return (const fault_descriptor_t *)0;
}

/**
 * @brief Extract the fault category byte from a fault code.
 * @param code  A FAULT_xxx constant.
 * @return      Category byte (0x01–0x07), or 0 for FAULT_NONE.
 */
static inline uint8_t fault_category(uint16_t code)
{
    return (uint8_t)((code >> 8U) & 0xFFU);
}

/**
 * @brief Return true if the fault is of safety severity.
 * @param code  A FAULT_xxx constant.
 */
static inline int fault_is_safety(uint16_t code)
{
    const fault_descriptor_t *d = fault_lookup(code);
    return (d != (const fault_descriptor_t *)0) && (d->severity == SEVERITY_SAFETY);
}

#ifdef __cplusplus
}
#endif

#endif /* FAULT_CODES_H */
