/**
 * @file    power_monitor.h
 * @brief   Power and battery monitoring driver for the Lumina LCU-100.
 *
 * This module tracks the state of the LPB-100 power/battery board.  On
 * multi-board variants the LPB publishes BATTERY_STATUS frames over CAN FD
 * (ID: LUMINA_ID_LPB_BATTERY_STATUS) at 1000 ms intervals.  On single-board
 * variants the LPB analogue/I2C registers are read directly; in that case the
 * same public API is used and the back-end implementation is selected at build
 * time via the LCU_VARIANT_INTEGRATED preprocessor symbol.
 *
 * Voltage thresholds assume a nominal 12 V lead-acid or LiFePO4 system:
 *
 *   BATT_RECOVERY_MV  12000 mV  — minimum to exit low-voltage lockout after reset
 *   BATT_WARN_MV      11000 mV  — first-stage warning, log + notify TMC
 *   BATT_CRITICAL_MV  10500 mV  — second-stage: inhibit LTE modem (saves 8–12 W peak)
 *   BATT_SHUTDOWN_MV   9500 mV  — third-stage: post all-red, assert inhibit, STOP mode
 *
 * Hysteresis is implemented in power_monitor_get_action(): once an action
 * level is latched it is only released by a system reset AND the bus voltage
 * recovering above BATT_RECOVERY_MV.
 *
 * LPB heartbeat timeout:
 *   If no BATTERY_STATUS frame is received for LPB_COMMS_TIMEOUT_MS the module
 *   assumes the battery is OK (fail-safe to allow continued operation) but logs
 *   FAULT_COMMS_CAN_LPB_LINK_LOSS and sets the SYSEVT_BATTERY_OK bit.
 *
 * Copyright (c) Lumina Traffic Systems Ltd.  All rights reserved.
 * SPDX-License-Identifier: Proprietary
 */

#ifndef POWER_MONITOR_H
#define POWER_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include "can_bus.h"
#include "lumina_can_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Voltage threshold constants  (millivolts, 12 V nominal system)
 * ========================================================================= */

/** Minimum voltage to clear a low-battery latch after system reset. */
#define BATT_RECOVERY_MV        12000U

/** Voltage below which a low-battery warning is issued. */
#define BATT_WARN_MV            11000U

/** Voltage below which non-essential loads (LTE modem) are inhibited. */
#define BATT_CRITICAL_MV        10500U

/** Voltage below which a safe-shutdown sequence is initiated. */
#define BATT_SHUTDOWN_MV         9500U

/* =========================================================================
 * Timeout and polling constants
 * ========================================================================= */

/** Power monitor task tick period in milliseconds. */
#define POWER_MONITOR_TASK_PERIOD_MS    1000U

/**
 * Time (ms) without a CAN BATTERY_STATUS frame before declaring LPB comms
 * loss.  Must be a multiple of POWER_MONITOR_TASK_PERIOD_MS.
 */
#define LPB_COMMS_TIMEOUT_MS            10000U

/** Safe-shutdown all-red hold time before asserting hardware inhibit (ms). */
#define POWER_SHUTDOWN_ALLRED_DWELL_MS  5000U

/* =========================================================================
 * Battery fault flag bitmask (battery_status_t.fault_flags)
 * ========================================================================= */

/** DC bus voltage below minimum operating level. */
#define PWRFLT_UNDERVOLTAGE     0x01U

/** DC bus voltage above over-voltage trip level. */
#define PWRFLT_OVERVOLTAGE      0x02U

/** Battery temperature above safe operating range. */
#define PWRFLT_OVERTEMP         0x04U

/** Individual battery cell fault reported by BMS. */
#define PWRFLT_CELL_FAULT       0x08U

/* =========================================================================
 * Battery status structure
 * ========================================================================= */

/**
 * @brief Decoded battery and power system status.
 *
 * Updated by power_monitor_handle_can_message() on each received
 * LUMINA_ID_LPB_BATTERY_STATUS frame.  Read-only outside this module;
 * obtain a snapshot via power_monitor_get_status().
 */
typedef struct
{
    /** Battery terminal voltage in millivolts. */
    uint16_t    voltage_mv;

    /** Battery current: positive = charging, negative = discharging (mA). */
    int16_t     current_ma;

    /** State of charge, 0–100 %. */
    uint8_t     soc_pct;

    /** Battery temperature in degrees Celsius. */
    int8_t      temp_celsius;

    /** True if the charger is active (bulk, absorption, or float stage). */
    bool        charger_active;

    /** True if the solar MPPT input is supplying current. */
    bool        solar_input_active;

    /**
     * Active fault bitmask.
     *   PWRFLT_UNDERVOLTAGE  0x01
     *   PWRFLT_OVERVOLTAGE   0x02
     *   PWRFLT_OVERTEMP      0x04
     *   PWRFLT_CELL_FAULT    0x08
     */
    uint8_t     fault_flags;
} battery_status_t;

/* =========================================================================
 * Power action enumeration
 * ========================================================================= */

/**
 * @brief Recommended power-conservation action based on battery voltage.
 *
 * Actions are strictly ordered; higher numeric values supersede lower ones.
 * Once PWR_ACTION_SAFE_SHUTDOWN is reached it is not cleared without a full
 * system reset and BATT_RECOVERY_MV being confirmed.
 */
typedef enum
{
    /** Battery voltage is healthy; no action required. */
    PWR_ACTION_NONE             = 0U,

    /** Voltage below BATT_WARN_MV; log warning and notify TMC. */
    PWR_ACTION_WARN             = 1U,

    /** Voltage below BATT_WARN_MV; additionally reduce RF transmit power. */
    PWR_ACTION_REDUCE_TX_POWER  = 2U,

    /**
     * Voltage below BATT_CRITICAL_MV; disable LTE modem to save 8–12 W peak
     * load.  Traffic control continues normally.
     */
    PWR_ACTION_INHIBIT_MODEM    = 3U,

    /**
     * Voltage below BATT_SHUTDOWN_MV; post all-red command, wait
     * POWER_SHUTDOWN_ALLRED_DWELL_MS, assert hardware inhibit, log fault,
     * then enter STM32 STOP mode.  Only cleared by a POR + BATT_RECOVERY_MV.
     */
    PWR_ACTION_SAFE_SHUTDOWN    = 4U,
} power_action_t;

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * @brief Initialise the power monitor module.
 *
 * Registers the CAN Rx callback for LUMINA_ID_LPB_BATTERY_STATUS, clears
 * internal state, and resets the LPB heartbeat watchdog timer.
 *
 * Must be called after can_bus_init() and before vTaskStartScheduler().
 *
 * @return true on success; false if the CAN callback registration failed.
 */
bool power_monitor_init(void);

/**
 * @brief Obtain a snapshot of the most recent battery status.
 *
 * Thread-safe copy protected by an internal critical section.  The snapshot
 * reflects the state decoded from the last received CAN BATTERY_STATUS frame
 * (or zeroed defaults if no frame has been received yet).
 *
 * @param[out] status_out  Caller-allocated struct to fill.  Must not be NULL.
 */
void power_monitor_get_status(battery_status_t *status_out);

/**
 * @brief Determine the recommended power-conservation action.
 *
 * Implements hysteresis-based voltage threshold comparison.  The returned
 * action level can only increase monotonically within a power-on cycle;
 * it is never downgraded except by a full system reset.
 *
 * Safe to call from any task.
 *
 * @return  power_action_t representing the most severe action level active.
 */
power_action_t power_monitor_get_action(void);

/**
 * @brief Power monitor periodic task (1000 ms period).
 *
 * Checks the LPB heartbeat watchdog, evaluates voltage thresholds, and
 * executes any required power-conservation action including the safe-shutdown
 * sequence.  Must be registered with xTaskCreate() in main.c.
 *
 * @param pvParameters  Unused; pass NULL.
 */
void power_monitor_task(void *pvParameters);

/**
 * @brief CAN Rx callback for LPB-100 BATTERY_STATUS frames.
 *
 * Registered with can_bus_register_rx_callback() for
 * LUMINA_ID_LPB_BATTERY_STATUS.  Decodes the lumina_battery_status_t
 * payload and updates the module's internal battery_status_t snapshot.
 * Resets the LPB heartbeat watchdog timer on every valid frame.
 *
 * Called from the can_rx_task context (NOT from ISR context).
 *
 * @param[in] msg  Pointer to the received CAN FD message.
 */
void power_monitor_handle_can_message(const can_message_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* POWER_MONITOR_H */
