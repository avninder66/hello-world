/**
 * @file    power_monitor.c
 * @brief   Power and battery monitoring driver implementation — Lumina LCU-100.
 *
 * Decodes BATTERY_STATUS CAN frames broadcast by the LPB-100 board, evaluates
 * voltage thresholds against the 12 V system limits, and escalates power-
 * conservation actions with one-way hysteresis:
 *
 *   voltage > BATT_WARN_MV                → PWR_ACTION_NONE
 *   voltage ≤ BATT_WARN_MV               → PWR_ACTION_WARN
 *   voltage ≤ BATT_CRITICAL_MV           → PWR_ACTION_INHIBIT_MODEM
 *   voltage ≤ BATT_SHUTDOWN_MV           → PWR_ACTION_SAFE_SHUTDOWN
 *
 * The action level is latched; it never decreases within a power cycle.
 * Recovery requires a system reset with the battery voltage above
 * BATT_RECOVERY_MV.
 *
 * Thread safety
 * -------------
 * power_monitor_handle_can_message() runs in the can_rx_task context.
 * power_monitor_task() runs in its own FreeRTOS task context.
 * All shared state is protected by a FreeRTOS mutex (g_status_mutex) except
 * the heartbeat watchdog tick counter which uses atomic 32-bit writes (safe
 * on Cortex-M7 due to single-cycle 32-bit bus access).
 *
 * Copyright (c) Lumina Traffic Systems Ltd.  All rights reserved.
 * SPDX-License-Identifier: Proprietary
 */

#include "power_monitor.h"
#include "fault_log.h"
#include "fault_codes.h"
#include "safety_comm.h"
#include "main.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"

#include <string.h>
#include <stddef.h>

/* =========================================================================
 * Private constants
 * ========================================================================= */

/**
 * Number of task ticks between LPB heartbeat watchdog checks.
 * POWER_MONITOR_TASK_PERIOD_MS == 1000 ms, timeout == 10000 ms → 10 ticks.
 */
#define LPB_TIMEOUT_TICKS \
    (LPB_COMMS_TIMEOUT_MS / POWER_MONITOR_TASK_PERIOD_MS)

/** Sentinel value stored in g_lpb_last_rx_tick when no frame received yet. */
#define LPB_TICK_NEVER      0U

/* =========================================================================
 * Private state
 * ========================================================================= */

/** Most recent decoded battery status. */
static battery_status_t g_battery_status;

/** Mutex protecting g_battery_status. */
static SemaphoreHandle_t g_status_mutex = NULL;

/**
 * HAL tick value (ms) of the last received LPB BATTERY_STATUS frame.
 * Written by power_monitor_handle_can_message() (can_rx_task context).
 * Read  by power_monitor_task().
 * Cortex-M7 32-bit aligned write is atomic; no mutex required.
 */
static volatile uint32_t g_lpb_last_rx_tick = LPB_TICK_NEVER;

/**
 * Latched power action — strictly non-decreasing within a power cycle.
 * Protected by g_status_mutex.
 */
static power_action_t g_latched_action = PWR_ACTION_NONE;

/**
 * Set to true once the LPB comms-loss fault has been logged so it is not
 * logged repeatedly every tick.
 */
static bool g_lpb_timeout_fault_logged = false;

/**
 * Set to true when the safe-shutdown sequence is in progress so the task
 * does not re-enter it on the next tick.
 */
static bool g_shutdown_in_progress = false;

/* =========================================================================
 * Private helpers
 * ========================================================================= */

/**
 * @brief Safely read the current battery voltage from the protected struct.
 * @return Battery terminal voltage in millivolts.
 */
static uint16_t prv_read_voltage_mv(void)
{
    uint16_t v = 0U;
    if (xSemaphoreTake(g_status_mutex, pdMS_TO_TICKS(5U)) == pdTRUE)
    {
        v = g_battery_status.voltage_mv;
        xSemaphoreGive(g_status_mutex);
    }
    return v;
}

/**
 * @brief Post all-red by requesting phase 0 via xPhaseCommandQueue.
 *
 * Phase ID 0 is the reserved all-red state in the traffic engine.
 */
static void prv_post_all_red(void)
{
    phase_request_t req;
    req.requested_phase_id = 0U;
    req.reason             = REASON_SAFETY;

    /* Best-effort: if the queue is full the traffic engine will handle it
     * independently via its own safety timeout. */
    (void)xQueueSend(xPhaseCommandQueue, &req, pdMS_TO_TICKS(50U));
}

/**
 * @brief Log a fault to the FRAM fault log.
 *
 * Convenience wrapper filling mandatory fields.
 *
 * @param fault_code  FAULT_xxx constant from fault_codes.h.
 * @param severity    fault_severity_t level.
 * @param extra0      Diagnostic byte 0 (e.g. voltage high byte).
 * @param extra1      Diagnostic byte 1 (e.g. voltage low byte).
 */
static void prv_log_fault(uint16_t fault_code,
                          fault_severity_t severity,
                          uint8_t extra0,
                          uint8_t extra1)
{
    (void)fault_log_record(fault_code,
                           severity,
                           (uint8_t)LUMINA_CAN_BASE_LPB,
                           0xFFU,   /* phase N/A */
                           extra0,
                           extra1,
                           0x00U,
                           0x00U);
}

/* =========================================================================
 * Safe-shutdown sequence
 *
 * Entered once voltage < BATT_SHUTDOWN_MV.  Flow:
 *   1. Post all-red command to traffic engine.
 *   2. Wait POWER_SHUTDOWN_ALLRED_DWELL_MS for signal heads to settle.
 *   3. Assert hardware inhibit line via safety_comm_emergency_inhibit().
 *   4. Log FAULT_POWER_BATTERY_CRITICAL with SEVERITY_SAFETY.
 *   5. Enter STM32H743 STOP mode (clocks gated, SRAM retained).
 *
 * This function does not return.
 * ========================================================================= */
static void prv_execute_safe_shutdown(uint16_t voltage_mv)
{
    g_shutdown_in_progress = true;

    /* Step 1 — command all-red. */
    prv_post_all_red();

    /* Step 2 — dwell to allow signal heads to respond. */
    vTaskDelay(pdMS_TO_TICKS(POWER_SHUTDOWN_ALLRED_DWELL_MS));

    /* Step 3 — assert hardware inhibit. */
    safety_comm_emergency_inhibit();
    HAL_GPIO_WritePin(INHIBIT_LINE_GPIO_Port,
                      INHIBIT_LINE_Pin,
                      GPIO_PIN_SET);

    /* Step 4 — log fault. */
    prv_log_fault(FAULT_POWER_BATTERY_CRITICAL,
                  SEVERITY_SAFETY,
                  (uint8_t)((voltage_mv >> 8U) & 0xFFU),
                  (uint8_t)(voltage_mv & 0xFFU));

    /* Step 5 — enter STOP mode.  Peripheral clocks are gated.  SRAM is
     * retained.  Only an external wakeup (EXTI) or POR will resume. */
    HAL_SuspendTick();
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

    /* Should never reach here. */
    while (1)
    {
        /* Spin until watchdog fires or POR occurs. */
    }
}

/* =========================================================================
 * Public API
 * ========================================================================= */

bool power_monitor_init(void)
{
    /* Clear all state. */
    memset(&g_battery_status, 0, sizeof(g_battery_status));
    g_lpb_last_rx_tick      = LPB_TICK_NEVER;
    g_latched_action        = PWR_ACTION_NONE;
    g_lpb_timeout_fault_logged = false;
    g_shutdown_in_progress  = false;

    /* Create the mutex protecting g_battery_status and g_latched_action. */
    g_status_mutex = xSemaphoreCreateMutex();
    if (g_status_mutex == NULL)
    {
        return false;
    }

    /* Register CAN Rx callback for LPB battery status frames. */
    if (!can_bus_register_rx_callback(LUMINA_ID_LPB_BATTERY_STATUS,
                                      power_monitor_handle_can_message))
    {
        return false;
    }

    return true;
}

/* -------------------------------------------------------------------------
 * power_monitor_handle_can_message
 * -------------------------------------------------------------------------
 * Called from can_rx_task context on reception of a BATTERY_STATUS frame
 * from the LPB-100.  Decodes lumina_battery_status_t and updates the module
 * snapshot.
 * ------------------------------------------------------------------------- */
void power_monitor_handle_can_message(const can_message_t *msg)
{
    if (msg == NULL)
    {
        return;
    }

    /* Validate ID and minimum payload length. */
    if ((msg->id != LUMINA_ID_LPB_BATTERY_STATUS) ||
        (msg->dlc < (uint8_t)sizeof(lumina_battery_status_t)))
    {
        return;
    }

    /* Overlay the payload struct directly. */
    lumina_battery_status_t raw;
    memcpy(&raw, msg->data, sizeof(raw));

    /* Build the local status snapshot. */
    battery_status_t local;
    local.voltage_mv       = raw.battery_voltage_mv;
    local.current_ma       = raw.battery_current_ma;
    local.soc_pct          = raw.soc_percent;

    /* LPB board temperature is offset +40 (raw 0 = -40 °C). */
    local.temp_celsius     = (int8_t)((int16_t)raw.board_temp_c - 40);

    local.charger_active   = (raw.charger_state == LUMINA_CHARGER_BULK)      ||
                             (raw.charger_state == LUMINA_CHARGER_ABSORPTION) ||
                             (raw.charger_state == LUMINA_CHARGER_FLOAT);

    /* Solar input: if charger is active and mains is NOT present, assume solar. */
    local.solar_input_active = local.charger_active && (!raw.mains_present);

    /* Build fault flags from LPB status bits. */
    local.fault_flags = 0U;
    if (raw.under_voltage)  { local.fault_flags |= PWRFLT_UNDERVOLTAGE; }
    if (raw.over_voltage)   { local.fault_flags |= PWRFLT_OVERVOLTAGE;  }
    if (raw.overtemp)       { local.fault_flags |= PWRFLT_OVERTEMP;     }

    /* Copy atomically under mutex. */
    if (xSemaphoreTake(g_status_mutex, pdMS_TO_TICKS(5U)) == pdTRUE)
    {
        g_battery_status = local;
        xSemaphoreGive(g_status_mutex);
    }

    /* Refresh heartbeat watchdog — atomic 32-bit write on Cortex-M7. */
    g_lpb_last_rx_tick = HAL_GetTick();

    /* If we previously logged an LPB timeout fault, clear the flag so a fresh
     * fault is logged if the link drops again. */
    g_lpb_timeout_fault_logged = false;
}

/* -------------------------------------------------------------------------
 * power_monitor_get_status
 * ------------------------------------------------------------------------- */
void power_monitor_get_status(battery_status_t *status_out)
{
    if (status_out == NULL)
    {
        return;
    }

    if (xSemaphoreTake(g_status_mutex, pdMS_TO_TICKS(10U)) == pdTRUE)
    {
        *status_out = g_battery_status;
        xSemaphoreGive(g_status_mutex);
    }
    else
    {
        /* Timeout — return a zeroed struct to avoid stale data. */
        memset(status_out, 0, sizeof(*status_out));
    }
}

/* -------------------------------------------------------------------------
 * power_monitor_get_action
 *
 * Hysteresis rules (actions only escalate, never de-escalate):
 *   voltage > BATT_WARN_MV     → PWR_ACTION_NONE       (if no latch)
 *   voltage ≤ BATT_WARN_MV     → PWR_ACTION_WARN
 *   voltage ≤ BATT_CRITICAL_MV → PWR_ACTION_INHIBIT_MODEM
 *   voltage ≤ BATT_SHUTDOWN_MV → PWR_ACTION_SAFE_SHUTDOWN
 * ------------------------------------------------------------------------- */
power_action_t power_monitor_get_action(void)
{
    uint16_t v = prv_read_voltage_mv();
    power_action_t new_action = PWR_ACTION_NONE;

    if (v == 0U)
    {
        /* No valid reading yet; assume OK. */
        return g_latched_action;
    }

    if (v <= BATT_SHUTDOWN_MV)
    {
        new_action = PWR_ACTION_SAFE_SHUTDOWN;
    }
    else if (v <= BATT_CRITICAL_MV)
    {
        new_action = PWR_ACTION_INHIBIT_MODEM;
    }
    else if (v <= BATT_WARN_MV)
    {
        new_action = PWR_ACTION_WARN;
    }
    else
    {
        new_action = PWR_ACTION_NONE;
    }

    /* Latch — only ratchet up, never down. */
    if (xSemaphoreTake(g_status_mutex, pdMS_TO_TICKS(5U)) == pdTRUE)
    {
        if (new_action > g_latched_action)
        {
            g_latched_action = new_action;
        }
        new_action = g_latched_action;
        xSemaphoreGive(g_status_mutex);
    }

    return new_action;
}

/* -------------------------------------------------------------------------
 * power_monitor_task
 *
 * 1000 ms periodic task.
 *   a) Check LPB heartbeat watchdog — log fault on timeout.
 *   b) Evaluate power action from latest voltage.
 *   c) Execute required action (warn / inhibit modem / safe shutdown).
 * ------------------------------------------------------------------------- */
void power_monitor_task(void *pvParameters)
{
    (void)pvParameters;

    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;)
    {
        vTaskDelayUntil(&xLastWakeTime,
                        pdMS_TO_TICKS(POWER_MONITOR_TASK_PERIOD_MS));

        /* ---- (a) LPB heartbeat watchdog ---------------------------------- */
        uint32_t now      = HAL_GetTick();
        uint32_t last_rx  = g_lpb_last_rx_tick;  /* atomic 32-bit read */

        bool lpb_timed_out = false;

        if (last_rx == LPB_TICK_NEVER)
        {
            /* No frame received since boot. */
            if (now >= LPB_COMMS_TIMEOUT_MS)
            {
                lpb_timed_out = true;
            }
        }
        else
        {
            if ((now - last_rx) >= LPB_COMMS_TIMEOUT_MS)
            {
                lpb_timed_out = true;
            }
        }

        if (lpb_timed_out && !g_lpb_timeout_fault_logged)
        {
            g_lpb_timeout_fault_logged = true;

            prv_log_fault(FAULT_COMMS_CAN_LPB_LINK_LOSS,
                          SEVERITY_WARNING,
                          0x00U,
                          0x00U);

            /* Set BATTERY_OK — fail-safe assumption: battery is OK when
             * comms are lost so traffic control continues. */
            xEventGroupSetBits(xSystemEventGroup, SYSEVT_BATTERY_OK);
        }

        /* If shutdown is already in progress, do not evaluate actions again. */
        if (g_shutdown_in_progress)
        {
            continue;
        }

        /* ---- (b) Evaluate power action ----------------------------------- */
        uint16_t voltage_mv = prv_read_voltage_mv();
        power_action_t action = power_monitor_get_action();

        /* ---- (c) Execute action ------------------------------------------ */
        switch (action)
        {
            case PWR_ACTION_NONE:
                /* Ensure BATTERY_OK event bit is set when voltage is healthy. */
                xEventGroupSetBits(xSystemEventGroup, SYSEVT_BATTERY_OK);
                break;

            case PWR_ACTION_REDUCE_TX_POWER:
                /* Fall through — WARN is the minimum; also set battery low. */
                /* FALLTHROUGH */
            case PWR_ACTION_WARN:
                /* Clear BATTERY_OK to signal degraded state to other tasks. */
                xEventGroupClearBits(xSystemEventGroup, SYSEVT_BATTERY_OK);
                /* Set BATTERY_LOW event so telemetry raises the alert. */
                xEventGroupSetBits(xSystemEventGroup, SYSEVT_FAULT_ACTIVE);

                prv_log_fault(FAULT_POWER_BATTERY_LOW,
                              SEVERITY_WARNING,
                              (uint8_t)((voltage_mv >> 8U) & 0xFFU),
                              (uint8_t)(voltage_mv & 0xFFU));
                break;

            case PWR_ACTION_INHIBIT_MODEM:
                /* Critical level — disable LTE modem to shed 8–12 W peak. */
                xEventGroupClearBits(xSystemEventGroup, SYSEVT_BATTERY_OK);
                xEventGroupSetBits(xSystemEventGroup, SYSEVT_FAULT_ACTIVE);

                prv_log_fault(FAULT_POWER_BATTERY_CRITICAL,
                              SEVERITY_CRITICAL,
                              (uint8_t)((voltage_mv >> 8U) & 0xFFU),
                              (uint8_t)(voltage_mv & 0xFFU));

                /* Signal the telemetry task to power down the modem.
                 * The telemetry module monitors SYSEVT_BATTERY_OK; clearing
                 * it causes the modem to be powered off on the next tick. */
                break;

            case PWR_ACTION_SAFE_SHUTDOWN:
                /* Does not return. */
                prv_execute_safe_shutdown(voltage_mv);
                break;

            default:
                break;
        }
    }
}
