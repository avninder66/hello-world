/**
 * @file    fault_log_table.c
 * @brief   Fault descriptor table — Lumina LCU-100 platform.
 *
 * Defines the global fault_table[] array that backs the fault_lookup() inline
 * function declared in fault_codes.h.  Every FAULT_xxx code defined in
 * fault_codes.h is represented here.
 *
 * Table format:
 *   { fault_code, severity, action, "Description string" }
 *
 * The table is terminated by a sentinel entry with code == FAULT_NONE (0x0000).
 *
 * fault_lookup() performs a linear search from the start of the table to the
 * sentinel.  The table is ordered by category (0x01xx – 0x07xx) to make it
 * easy to audit completeness against fault_codes.h.
 *
 * Copyright (c) Lumina Traffic Systems Ltd.  All rights reserved.
 * SPDX-License-Identifier: Proprietary
 */

#include "fault_codes.h"

#include <stddef.h>

/* =========================================================================
 * Fault descriptor table
 *
 * Columns: code | severity | action | description
 * ========================================================================= */

const fault_descriptor_t fault_table[] =
{
    /* -----------------------------------------------------------------------
     * Category 0x01 — MCU faults
     * --------------------------------------------------------------------- */

    {
        FAULT_MCU_WATCHDOG_EXPIRED,
        SEVERITY_CRITICAL,
        ACTION_SAFE_SHUTDOWN,
        "Watchdog timer expired without being petted; MCU reset occurred"
    },
    {
        FAULT_MCU_STACK_OVERFLOW,
        SEVERITY_SAFETY,
        ACTION_SAFE_SHUTDOWN,
        "Stack overflow detected in an RTOS task"
    },
    {
        FAULT_MCU_FLASH_CRC_ERROR,
        SEVERITY_SAFETY,
        ACTION_SAFE_SHUTDOWN,
        "Runtime flash CRC check failed; firmware integrity not confirmed"
    },
    {
        FAULT_MCU_RAM_ECC_ERROR,
        SEVERITY_CRITICAL,
        ACTION_SAFE_SHUTDOWN,
        "SRAM bitflip detected by scrubbing routine (ECC or walk-pattern)"
    },
    {
        FAULT_MCU_OVERTEMPERATURE,
        SEVERITY_CRITICAL,
        ACTION_SAFE_SHUTDOWN,
        "CPU die temperature exceeded 110 C; thermal runaway risk"
    },
    {
        FAULT_MCU_CLOCK_FAULT,
        SEVERITY_SAFETY,
        ACTION_SAFE_SHUTDOWN,
        "Internal oscillator or PLL lock failure; clock integrity suspect"
    },
    {
        FAULT_MCU_DMA_ERROR,
        SEVERITY_CRITICAL,
        ACTION_ALARM,
        "DMA transfer error on a critical peripheral (SPI, CAN, UART)"
    },
    {
        FAULT_MCU_TASK_DEADLINE_MISS,
        SEVERITY_WARNING,
        ACTION_ALARM,
        "RTOS task exceeded its scheduling deadline"
    },
    {
        FAULT_MCU_NVM_WRITE_FAIL,
        SEVERITY_CRITICAL,
        ACTION_LOG_ONLY,
        "NVM (internal flash) write or erase operation failed"
    },
    {
        FAULT_MCU_ADC_REFERENCE_FAULT,
        SEVERITY_WARNING,
        ACTION_ALARM,
        "Internal ADC reference voltage out of tolerance"
    },

    /* -----------------------------------------------------------------------
     * Category 0x02 — Safety faults
     * --------------------------------------------------------------------- */

    {
        FAULT_SAFETY_CONFLICT_DETECTED,
        SEVERITY_SAFETY,
        ACTION_SAFE_SHUTDOWN,
        "Phase activation would create a conflicting concurrent green display"
    },
    {
        FAULT_SAFETY_SUPERVISOR_WDG_EXPIRED,
        SEVERITY_SAFETY,
        ACTION_SAFE_SHUTDOWN,
        "Safety supervisor SPI watchdog expired; link to STM32G071 lost"
    },
    {
        FAULT_SAFETY_INHIBIT_RELAY_FAIL,
        SEVERITY_SAFETY,
        ACTION_SAFE_SHUTDOWN,
        "Supervisor output inhibit relay failed to assert when commanded"
    },
    {
        FAULT_SAFETY_CONFLICT_MATRIX_CRC,
        SEVERITY_SAFETY,
        ACTION_SAFE_SHUTDOWN,
        "Conflict matrix stored in supervisor NVM failed CRC validation"
    },
    {
        FAULT_SAFETY_INTERGREEN_TIMER_FAULT,
        SEVERITY_SAFETY,
        ACTION_SAFE_SHUTDOWN,
        "Intergreen timer elapsed faster than allowed by hardware timer"
    },
    {
        FAULT_SAFETY_SELF_TEST_FAIL,
        SEVERITY_SAFETY,
        ACTION_SAFE_SHUTDOWN,
        "Supervisor reported a self-test failure during startup sequence"
    },
    {
        FAULT_SAFETY_VERSION_MISMATCH,
        SEVERITY_CRITICAL,
        ACTION_SAFE_SHUTDOWN,
        "Safety supervisor firmware version mismatch with main MCU protocol"
    },
    {
        FAULT_SAFETY_RED_CONFIRM_TIMEOUT,
        SEVERITY_SAFETY,
        ACTION_SAFE_SHUTDOWN,
        "Red channel was not confirmed on during all-red transition"
    },
    {
        FAULT_SAFETY_INHIBIT_LINE_STUCK,
        SEVERITY_SAFETY,
        ACTION_SAFE_SHUTDOWN,
        "Supervisor hardware inhibit line is stuck de-asserted"
    },
    {
        FAULT_SAFETY_PHASE_TIMEOUT,
        SEVERITY_CRITICAL,
        ACTION_SAFE_SHUTDOWN,
        "Phase dwell time exceeded plan maximum; forced termination applied"
    },

    /* -----------------------------------------------------------------------
     * Category 0x03 — Output faults (LSO-100)
     * --------------------------------------------------------------------- */

    {
        FAULT_OUTPUT_OPEN_CIRCUIT_CH1,
        SEVERITY_WARNING,
        ACTION_INHIBIT_OUTPUT,
        "Open circuit detected on lamp channel 1 (PROFET IS feedback = 0)"
    },
    {
        FAULT_OUTPUT_OPEN_CIRCUIT_CH2,
        SEVERITY_WARNING,
        ACTION_INHIBIT_OUTPUT,
        "Open circuit detected on lamp channel 2"
    },
    {
        FAULT_OUTPUT_OPEN_CIRCUIT_CH3,
        SEVERITY_WARNING,
        ACTION_INHIBIT_OUTPUT,
        "Open circuit detected on lamp channel 3"
    },
    {
        FAULT_OUTPUT_OPEN_CIRCUIT_CH4,
        SEVERITY_WARNING,
        ACTION_INHIBIT_OUTPUT,
        "Open circuit detected on lamp channel 4"
    },
    {
        FAULT_OUTPUT_OPEN_CIRCUIT_CH5,
        SEVERITY_WARNING,
        ACTION_INHIBIT_OUTPUT,
        "Open circuit detected on lamp channel 5"
    },
    {
        FAULT_OUTPUT_OPEN_CIRCUIT_CH6,
        SEVERITY_WARNING,
        ACTION_INHIBIT_OUTPUT,
        "Open circuit detected on lamp channel 6"
    },
    {
        FAULT_OUTPUT_OPEN_CIRCUIT_CH7,
        SEVERITY_WARNING,
        ACTION_INHIBIT_OUTPUT,
        "Open circuit detected on lamp channel 7"
    },
    {
        FAULT_OUTPUT_OPEN_CIRCUIT_CH8,
        SEVERITY_WARNING,
        ACTION_INHIBIT_OUTPUT,
        "Open circuit detected on lamp channel 8"
    },
    {
        FAULT_OUTPUT_SHORT_CIRCUIT,
        SEVERITY_CRITICAL,
        ACTION_INHIBIT_OUTPUT,
        "Short circuit or overcurrent trip on lamp channel (channel in fault_data)"
    },
    {
        FAULT_OUTPUT_PROFET_THERMAL,
        SEVERITY_CRITICAL,
        ACTION_INHIBIT_OUTPUT,
        "PROFET device over-temperature thermal shutdown (channel in fault_data)"
    },
    {
        FAULT_OUTPUT_LOAD_CURRENT_HIGH,
        SEVERITY_WARNING,
        ACTION_ALARM,
        "Load current exceeds expected range; lamp wattage suspect"
    },
    {
        FAULT_OUTPUT_LOAD_CURRENT_LOW,
        SEVERITY_WARNING,
        ACTION_ALARM,
        "Load current below expected range; partial lamp failure or open circuit"
    },
    {
        FAULT_OUTPUT_STATE_MISMATCH,
        SEVERITY_CRITICAL,
        ACTION_SAFE_SHUTDOWN,
        "Output state does not match commanded state after settling time"
    },
    {
        FAULT_OUTPUT_SUPPLY_FAULT,
        SEVERITY_CRITICAL,
        ACTION_SAFE_SHUTDOWN,
        "LSO board supply rail out of tolerance; all output quality suspect"
    },

    /* -----------------------------------------------------------------------
     * Category 0x04 — Power faults (LPB-100)
     * --------------------------------------------------------------------- */

    {
        FAULT_POWER_BATTERY_CRITICAL,
        SEVERITY_SAFETY,
        ACTION_SAFE_SHUTDOWN,
        "Battery voltage has reached the critical shutdown threshold"
    },
    {
        FAULT_POWER_BATTERY_LOW,
        SEVERITY_WARNING,
        ACTION_ALARM,
        "Battery voltage is low; reduced runtime remaining"
    },
    {
        FAULT_POWER_BUS_OVER_VOLTAGE,
        SEVERITY_CRITICAL,
        ACTION_SAFE_SHUTDOWN,
        "DC bus voltage exceeds over-voltage trip threshold"
    },
    {
        FAULT_POWER_BUS_UNDER_VOLTAGE,
        SEVERITY_CRITICAL,
        ACTION_SAFE_SHUTDOWN,
        "DC bus voltage below under-voltage lockout threshold"
    },
    {
        FAULT_POWER_MAINS_LOST,
        SEVERITY_WARNING,
        ACTION_ALARM,
        "Mains AC input has been lost; operating on battery"
    },
    {
        FAULT_POWER_BATTERY_NOT_PRESENT,
        SEVERITY_CRITICAL,
        ACTION_ALARM,
        "Battery is not connected or has been removed"
    },
    {
        FAULT_POWER_BATTERY_TEMP_FAULT,
        SEVERITY_WARNING,
        ACTION_ALARM,
        "Battery temperature sensor reading out of range or open circuit"
    },
    {
        FAULT_POWER_BATTERY_OVERTEMP,
        SEVERITY_CRITICAL,
        ACTION_ALARM,
        "Battery cell over-temperature fault; charging suspended"
    },
    {
        FAULT_POWER_CHARGER_FAULT,
        SEVERITY_WARNING,
        ACTION_ALARM,
        "Charger PWM controller fault; charging capability lost"
    },
    {
        FAULT_POWER_5V_RAIL_FAULT,
        SEVERITY_WARNING,
        ACTION_ALARM,
        "LPB internal 5 V regulation fault; sensor data unreliable"
    },

    /* -----------------------------------------------------------------------
     * Category 0x05 — Communications faults
     * --------------------------------------------------------------------- */

    {
        FAULT_COMMS_CAN_LSO_LINK_LOSS,
        SEVERITY_CRITICAL,
        ACTION_SAFE_SHUTDOWN,
        "CAN FD link to LSO-100 signal output board lost (heartbeat timeout)"
    },
    {
        FAULT_COMMS_CAN_LPB_LINK_LOSS,
        SEVERITY_WARNING,
        ACTION_ALARM,
        "CAN FD link to LPB-100 power board lost (heartbeat timeout)"
    },
    {
        FAULT_COMMS_CAN_LPI_LINK_LOSS,
        SEVERITY_WARNING,
        ACTION_ALARM,
        "CAN FD link to LPI-100 pedestrian interface lost (heartbeat timeout)"
    },
    {
        FAULT_COMMS_CAN_SAFETY_LINK_LOSS,
        SEVERITY_SAFETY,
        ACTION_SAFE_SHUTDOWN,
        "CAN FD link to safety supervisor lost (heartbeat timeout)"
    },
    {
        FAULT_COMMS_CAN_BUS_OFF,
        SEVERITY_CRITICAL,
        ACTION_SAFE_SHUTDOWN,
        "CAN FD bus-off condition detected; controller has withdrawn from bus"
    },
    {
        FAULT_COMMS_CAN_FRAME_ERROR,
        SEVERITY_WARNING,
        ACTION_ALARM,
        "CAN FD message received with CRC error (error passive threshold)"
    },
    {
        FAULT_COMMS_RS485_FRAME_ERROR,
        SEVERITY_WARNING,
        ACTION_LOG_ONLY,
        "RS485 uplink UART framing or parity error (remote monitoring)"
    },
    {
        FAULT_COMMS_RS485_TIMEOUT,
        SEVERITY_WARNING,
        ACTION_LOG_ONLY,
        "RS485 uplink response timeout from remote monitoring centre"
    },
    {
        FAULT_COMMS_SPI_CRC_ERROR,
        SEVERITY_CRITICAL,
        ACTION_ALARM,
        "SPI to safety supervisor: CRC error threshold exceeded"
    },
    {
        FAULT_COMMS_SPI_TIMEOUT,
        SEVERITY_CRITICAL,
        ACTION_SAFE_SHUTDOWN,
        "SPI to safety supervisor: transfer did not complete within deadline"
    },

    /* -----------------------------------------------------------------------
     * Category 0x06 — Configuration faults
     * --------------------------------------------------------------------- */

    {
        FAULT_CONFIG_PLAN_CRC_FAIL,
        SEVERITY_CRITICAL,
        ACTION_SAFE_SHUTDOWN,
        "Loaded phase plan failed CRC32 validation"
    },
    {
        FAULT_CONFIG_INVALID_CHANNEL_MAP,
        SEVERITY_CRITICAL,
        ACTION_SAFE_SHUTDOWN,
        "Phase plan specifies an approach channel index beyond LSO capacity"
    },
    {
        FAULT_CONFIG_PLAN_PHASE_COUNT,
        SEVERITY_CRITICAL,
        ACTION_SAFE_SHUTDOWN,
        "Phase plan has zero phases or more than MAX_PHASES"
    },
    {
        FAULT_CONFIG_PHASE_TIMER_INVALID,
        SEVERITY_CRITICAL,
        ACTION_SAFE_SHUTDOWN,
        "Phase minimum duration is greater than maximum duration"
    },
    {
        FAULT_CONFIG_NVM_WRITE_FAIL,
        SEVERITY_CRITICAL,
        ACTION_LOG_ONLY,
        "NVM configuration sector failed to erase or write during provisioning"
    },
    {
        FAULT_CONFIG_NVM_INVALID_MAGIC,
        SEVERITY_WARNING,
        ACTION_ALARM,
        "NVM configuration header magic number invalid; factory defaults used"
    },
    {
        FAULT_CONFIG_INTERGREEN_OVERFLOW,
        SEVERITY_CRITICAL,
        ACTION_SAFE_SHUTDOWN,
        "Intergreen time matrix contains a value that exceeds the allowed maximum"
    },
    {
        FAULT_CONFIG_UNSUPPORTED_MODE,
        SEVERITY_WARNING,
        ACTION_ALARM,
        "Operating mode requested is not supported by the loaded phase plan"
    },
    {
        FAULT_CONFIG_CONFLICT_MATRIX_INVALID,
        SEVERITY_CRITICAL,
        ACTION_SAFE_SHUTDOWN,
        "Approach conflict matrix is inconsistent (asymmetric or self-conflicting)"
    },
    {
        FAULT_CONFIG_PLAN_VERSION_MISMATCH,
        SEVERITY_CRITICAL,
        ACTION_SAFE_SHUTDOWN,
        "Phase plan version is incompatible with current firmware version"
    },

    /* -----------------------------------------------------------------------
     * Category 0x07 — Hardware faults
     * --------------------------------------------------------------------- */

    {
        FAULT_HW_I2C_BUS_FAULT,
        SEVERITY_CRITICAL,
        ACTION_ALARM,
        "I2C bus to on-board RTC or EEPROM has failed"
    },
    {
        FAULT_HW_RTC_OSCILLATOR_FAIL,
        SEVERITY_WARNING,
        ACTION_ALARM,
        "On-board real-time clock battery dead or RTC oscillator stopped"
    },
    {
        FAULT_HW_TEMP_SENSOR_FAULT,
        SEVERITY_WARNING,
        ACTION_LOG_ONLY,
        "Ambient temperature sensor (NTC) open circuit or shorted"
    },
    {
        FAULT_HW_DETECTOR_INPUT_FAULT,
        SEVERITY_WARNING,
        ACTION_ALARM,
        "Optocoupler detector input signal quality degraded"
    },
    {
        FAULT_HW_GATE_DRIVE_SUPPLY,
        SEVERITY_CRITICAL,
        ACTION_SAFE_SHUTDOWN,
        "24 V PROFET gate drive supply rail fault"
    },
    {
        FAULT_HW_BOARD_ID_MISMATCH,
        SEVERITY_WARNING,
        ACTION_ALARM,
        "Hardware revision register read-back mismatch; board ID suspect"
    },
    {
        FAULT_HW_CRYSTAL_FAIL,
        SEVERITY_CRITICAL,
        ACTION_ALARM,
        "External crystal oscillator has failed; using internal RC oscillator"
    },
    {
        FAULT_HW_CAN_DCDC_FAULT,
        SEVERITY_CRITICAL,
        ACTION_SAFE_SHUTDOWN,
        "Isolated DC-DC converter for CAN PHY has failed"
    },
    {
        FAULT_HW_ENCLOSURE_TAMPER,
        SEVERITY_WARNING,
        ACTION_ALARM,
        "Magnetic reed switch for enclosure tamper detection opened"
    },
    {
        FAULT_HW_EXTERNAL_FAULT_LATCH,
        SEVERITY_CRITICAL,
        ACTION_SAFE_SHUTDOWN,
        "Hardware fault latch pin driven active by an external protection IC"
    },

    /* -----------------------------------------------------------------------
     * Sentinel — must be last
     * --------------------------------------------------------------------- */
    {
        FAULT_NONE,
        SEVERITY_INFO,
        ACTION_LOG_ONLY,
        (const char *)0
    }
};

/* =========================================================================
 * Table size and lookup function
 *
 * fault_lookup() is provided as a static inline in fault_codes.h, so only
 * fault_table_size and the definition of fault_table[] live here.
 * ========================================================================= */

/** Number of entries in fault_table[] excluding the sentinel. */
const size_t fault_table_size =
    (sizeof(fault_table) / sizeof(fault_table[0])) - 1U;
