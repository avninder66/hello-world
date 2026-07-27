/**
 * @file    traffic_engine.c
 * @brief   LCU-100 Traffic Engine — state machine implementation
 *
 * Runs as a FreeRTOS task at 50 ms intervals.  All external API calls are
 * serialised through xTEMutex.  Safety permissive requests and CAN phase
 * commands are issued via inter-task queues so this task never blocks on
 * peripheral I/O.
 *
 * State-machine transitions:
 *
 *   STARTUP ──(both ready)──→ ALL_RED
 *   STARTUP ──(10 s timeout)──→ FAULT_LOCKOUT
 *
 *   ALL_RED ──(permissive granted)──→ PHASE_ACTIVE
 *   ALL_RED ──(safety denied)──────→ FAULT_LOCKOUT
 *
 *   PHASE_ACTIVE ──(timer / demand)──→ AMBER_CLEARANCE
 *   PHASE_ACTIVE ──(fault)──────────→ FAULT_LOCKOUT
 *
 *   AMBER_CLEARANCE ──(intergreen expired)──→ ALL_RED_CLEARANCE
 *
 *   ALL_RED_CLEARANCE ──(all-red dwell expired)──→ ALL_RED (next phase)
 *
 *   FAULT_LOCKOUT ──(fault cleared + manual reset)──→ ALL_RED
 *
 *   any ──(MANUAL_OVERRIDE mode set)──→ MANUAL_OVERRIDE
 *   any ──(BLACKOUT mode set)─────────→ BLACKOUT
 */

#include "traffic_engine.h"
#include "safety_comm.h"
#include "main.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"
#include "semphr.h"
#include "timers.h"

#include <string.h>
#include <stddef.h>

/* ============================================================================
 * Private constants
 * ============================================================================ */
#define TE_TICK_MS              50U     /**< State-machine tick period (ms)          */
#define TE_STARTUP_TIMEOUT_MS   10000U  /**< Max wait for safety+CAN ready           */
#define TE_ALL_RED_MIN_MS       2000U   /**< Minimum ALL_RED dwell before permissive */
#define TE_FAULT_INHIBIT_MS     500U    /**< Inhibit assert settle time (ms)         */
#define TE_PHASE_REQ_TIMEOUT_MS 3000U   /**< Max time waiting for permissive grant   */

/** CAN arbitration IDs for phase commands to LSO modules.
 *  LSO approach N uses base address + N.                               */
#define CAN_LSO_CMD_BASE_ID     0x080U
#define CAN_LSO_BROADCAST_ID    0x07FU  /**< Addressed to all LSOs simultaneously   */

/** CAN data byte indices for the LSO command frame */
#define LSO_CMD_PHASE_ID_IDX    0
#define LSO_CMD_APPROACH_IDX    1
#define LSO_CMD_ASPECT_IDX      2
#define LSO_CMD_DURATION_HI_IDX 3
#define LSO_CMD_DURATION_LO_IDX 4

/* ============================================================================
 * CRC-16/CCITT-FALSE — used to validate loaded phase plans
 * ============================================================================ */
static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i] << 8);
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x8000U) {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/* ============================================================================
 * Module-level state
 * ============================================================================ */

/** Singleton context — all fields protected by xTEMutex when accessed externally */
static traffic_engine_context_t s_ctx;

/** Operating mode — set atomically via traffic_engine_set_mode() */
static volatile traffic_engine_mode_t s_mode = TE_MODE_NORMAL;

/** Phase request queue — internal to the engine (depth 4) */
static QueueHandle_t xPhaseRequestQueue = NULL;

/** Mutex protecting s_ctx and s_mode for external callers */
static SemaphoreHandle_t xTEMutex = NULL;

/** Publicly accessible pointer to the loaded plan */
const phase_plan_t *gpActivePhasePlan = NULL;

/* ============================================================================
 * Forward declarations (private helpers)
 * ============================================================================ */
static uint8_t         select_next_phase(void);
static HAL_StatusTypeDef command_approach_aspect(uint8_t approach_id,
                                                  uint8_t aspect,
                                                  uint8_t phase_id);
static void            command_all_red(void);
static void            command_all_dark(void);
static void            log_phase_event(fault_code_t code,
                                       fault_severity_t sev,
                                       uint32_t ctx0, uint32_t ctx1);
static bool            validate_plan_crc(const phase_plan_t *plan);
static const phase_descriptor_t *find_phase(uint8_t phase_id);

/* ============================================================================
 * traffic_engine_init
 * ============================================================================ */
bool traffic_engine_init(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.current_state   = STATE_STARTUP;
    s_ctx.current_phase_id = 0;
    s_ctx.next_phase_id    = 0;
    s_mode                 = TE_MODE_NORMAL;

    xTEMutex = xSemaphoreCreateMutex();
    if (xTEMutex == NULL) {
        return false;
    }

    xPhaseRequestQueue = xQueueCreate(4U, sizeof(phase_request_t));
    if (xPhaseRequestQueue == NULL) {
        vSemaphoreDelete(xTEMutex);
        return false;
    }

    return true;
}

/* ============================================================================
 * traffic_engine_task — FreeRTOS task entry point
 * ============================================================================ */
void traffic_engine_task(void *pvParameters)
{
    (void)pvParameters;

    /* Initialise engine internals if not already done by main */
    if (xTEMutex == NULL) {
        if (!traffic_engine_init()) {
            vTaskSuspend(NULL);
            return;
        }
    }

    TickType_t       xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod       = pdMS_TO_TICKS(TE_TICK_MS);

    /* Timestamps for state entry */
    uint32_t state_entry_tick = HAL_GetTick();
    uint32_t permissive_req_tick = 0;
    bool     permissive_requested = false;

    for (;;) {
        /* ---------------------------------------------------------------
         * Check for a mode override — highest-priority external input
         * --------------------------------------------------------------- */
        traffic_engine_mode_t current_mode = s_mode;  /* atomic read */

        if (current_mode == TE_MODE_MANUAL_OVERRIDE &&
            s_ctx.current_state != STATE_MANUAL_OVERRIDE) {
            s_ctx.current_state = STATE_MANUAL_OVERRIDE;
            state_entry_tick    = HAL_GetTick();
            permissive_requested = false;
        } else if (current_mode == TE_MODE_BLACKOUT &&
                   s_ctx.current_state != STATE_BLACKOUT) {
            command_all_dark();
            s_ctx.current_state = STATE_BLACKOUT;
            state_entry_tick    = HAL_GetTick();
            permissive_requested = false;
        }

        /* ---------------------------------------------------------------
         * Main state machine
         * --------------------------------------------------------------- */
        uint32_t now = HAL_GetTick();
        s_ctx.phase_elapsed_ms = now - s_ctx.phase_start_tick;

        switch (s_ctx.current_state) {

        /* ==============================================================
         * STATE_STARTUP
         * Wait for safety supervisor ready and CAN healthy before
         * allowing any phase commands.  10 s timeout → FAULT_LOCKOUT.
         * ============================================================== */
        case STATE_STARTUP: {
            EventBits_t bits = xEventGroupWaitBits(
                xSystemEventGroup,
                SYSEVT_SAFETY_READY | SYSEVT_CAN_HEALTHY,
                pdFALSE,   /* do not clear on exit */
                pdTRUE,    /* wait for ALL bits */
                pdMS_TO_TICKS(TE_STARTUP_TIMEOUT_MS));

            if ((bits & (SYSEVT_SAFETY_READY | SYSEVT_CAN_HEALTHY)) ==
                         (SYSEVT_SAFETY_READY | SYSEVT_CAN_HEALTHY)) {
                /* Both subsystems healthy — proceed to ALL_RED */
                command_all_red();
                s_ctx.current_state = STATE_ALL_RED;
                state_entry_tick    = HAL_GetTick();
                s_ctx.phase_start_tick = HAL_GetTick();

                /* Select first phase from plan */
                if (gpActivePhasePlan != NULL) {
                    s_ctx.next_phase_id = gpActivePhasePlan->start_phase_id;
                } else {
                    s_ctx.next_phase_id = 1U;
                }

                log_phase_event(FAULT_NONE, FAULT_SEV_INFO,
                                (uint32_t)STATE_STARTUP, (uint32_t)STATE_ALL_RED);
            } else {
                /* Timeout — at least one subsystem not ready */
                log_phase_event(FAULT_SAFETY_COMM_TIMEOUT,
                                FAULT_SEV_FATAL,
                                (uint32_t)bits,
                                TE_STARTUP_TIMEOUT_MS);
                command_all_dark();
                xEventGroupSetBits(xSystemEventGroup, SYSEVT_FAULT_ACTIVE);
                s_ctx.current_state = STATE_FAULT_LOCKOUT;
                s_ctx.fault_count++;
                state_entry_tick = HAL_GetTick();
            }
            /* Re-arm wake time after the potentially-long wait */
            xLastWakeTime = xTaskGetTickCount();
            break;
        }

        /* ==============================================================
         * STATE_ALL_RED
         * Command all-red, request permissive for next phase.
         * Minimum dwell is TE_ALL_RED_MIN_MS; then wait for permissive.
         * ============================================================== */
        case STATE_ALL_RED: {
            uint32_t dwell = now - state_entry_tick;

            if (dwell < TE_ALL_RED_MIN_MS) {
                /* Still in minimum dwell — nothing to do */
                break;
            }

            /* Request permissive for the next phase (once per state entry) */
            if (!permissive_requested) {
                permissive_result_t result =
                    safety_comm_request_permissive(s_ctx.next_phase_id);
                permissive_req_tick  = HAL_GetTick();
                permissive_requested = true;

                if (result == PERMISSIVE_GRANTED) {
                    goto permissive_granted;
                }
                if (result == PERMISSIVE_DENIED_FAULT ||
                    result == PERMISSIVE_DENIED_INHIBIT) {
                    goto permissive_denied;
                }
                /* PERMISSIVE_PENDING — will be polled next tick */
                break;
            }

            /* Poll permissive result on subsequent ticks */
            {
                safety_status_t status;
                safety_comm_get_status(&status);

                if (status.last_permissive_result == PERMISSIVE_GRANTED) {
                    goto permissive_granted;
                }

                if (status.last_permissive_result == PERMISSIVE_DENIED_FAULT ||
                    status.last_permissive_result == PERMISSIVE_DENIED_INHIBIT) {
                    goto permissive_denied;
                }

                /* Check timeout on permissive request */
                if ((HAL_GetTick() - permissive_req_tick) > TE_PHASE_REQ_TIMEOUT_MS) {
                    log_phase_event(FAULT_SAFETY_COMM_TIMEOUT,
                                    FAULT_SEV_CRITICAL,
                                    (uint32_t)s_ctx.next_phase_id,
                                    (uint32_t)status.last_permissive_result);
                    goto permissive_denied;
                }
            }
            break;

        permissive_granted: {
                /* Safety supervisor approved — release inhibit and start phase */
                HAL_GPIO_WritePin(INHIBIT_LINE_GPIO_Port, INHIBIT_LINE_Pin, GPIO_PIN_RESET);
                xEventGroupClearBits(xSystemEventGroup, SYSEVT_FAULT_ACTIVE);

                s_ctx.current_phase_id = s_ctx.next_phase_id;
                s_ctx.phase_start_tick = HAL_GetTick();
                s_ctx.phase_elapsed_ms = 0;
                permissive_requested   = false;
                s_ctx.current_state    = STATE_PHASE_ACTIVE;
                state_entry_tick       = HAL_GetTick();
                xEventGroupSetBits(xSystemEventGroup, SYSEVT_PHASE_ACTIVE);

                /* Issue CAN phase command for each approach defined in the phase */
                const phase_descriptor_t *pd = find_phase(s_ctx.current_phase_id);
                if (pd != NULL) {
                    for (uint8_t a = 0; a < pd->num_approaches; a++) {
                        command_approach_aspect(a, pd->approach_aspects[a],
                                                s_ctx.current_phase_id);
                    }
                }

                log_phase_event(FAULT_NONE, FAULT_SEV_INFO,
                                (uint32_t)s_ctx.current_phase_id,
                                (uint32_t)STATE_PHASE_ACTIVE);
                break;
            }

        permissive_denied: {
                command_all_red();
                HAL_GPIO_WritePin(INHIBIT_LINE_GPIO_Port, INHIBIT_LINE_Pin, GPIO_PIN_SET);
                log_phase_event(FAULT_SAFETY_PERMISSIVE_DENIED,
                                FAULT_SEV_CRITICAL,
                                (uint32_t)s_ctx.next_phase_id, 0);
                xEventGroupSetBits(xSystemEventGroup, SYSEVT_FAULT_ACTIVE);
                s_ctx.current_state = STATE_FAULT_LOCKOUT;
                s_ctx.fault_count++;
                permissive_requested = false;
                state_entry_tick = HAL_GetTick();
                break;
            }
        } /* end STATE_ALL_RED */
        break;  /* suppress fall-through warning from goto labels */

        /* ==============================================================
         * STATE_PHASE_ACTIVE
         * Monitor phase timer and demand flags.
         * Transition to AMBER_CLEARANCE when done.
         * ============================================================== */
        case STATE_PHASE_ACTIVE: {
            /* Abort if a fault appears */
            if (xEventGroupGetBits(xSystemEventGroup) & SYSEVT_FAULT_ACTIVE) {
                command_all_red();
                HAL_GPIO_WritePin(INHIBIT_LINE_GPIO_Port, INHIBIT_LINE_Pin, GPIO_PIN_SET);
                s_ctx.current_state = STATE_FAULT_LOCKOUT;
                s_ctx.fault_count++;
                state_entry_tick = HAL_GetTick();
                log_phase_event(FAULT_SAFETY_COMM_TIMEOUT, FAULT_SEV_CRITICAL,
                                (uint32_t)s_ctx.current_phase_id, now);
                break;
            }

            const phase_descriptor_t *pd = find_phase(s_ctx.current_phase_id);
            if (pd == NULL) {
                /* Plan corruption — bail out */
                log_phase_event(FAULT_PHASE_PLAN_INVALID, FAULT_SEV_FATAL,
                                (uint32_t)s_ctx.current_phase_id, 0);
                s_ctx.current_state = STATE_FAULT_LOCKOUT;
                s_ctx.fault_count++;
                state_entry_tick = HAL_GetTick();
                break;
            }

            bool phase_change_needed = false;

            /* Maximum green timer expired? */
            if (s_ctx.phase_elapsed_ms >= pd->max_green_ms) {
                phase_change_needed = true;
                s_ctx.next_phase_id = select_next_phase();
            }

            /* Demand-based change: minimum green elapsed and a demand is pending */
            if (!phase_change_needed &&
                pd->demand_enabled &&
                s_ctx.phase_elapsed_ms >= pd->min_green_ms &&
                s_ctx.demand_flags != 0U) {
                /* Find the highest-priority demand approach */
                for (uint8_t b = 0; b < PHASE_PLAN_MAX_APPROACHES; b++) {
                    if (s_ctx.demand_flags & (1U << b)) {
                        s_ctx.demand_flags &= ~(1U << b);  /* consume demand */
                        phase_change_needed = true;
                        s_ctx.next_phase_id = select_next_phase();
                        break;
                    }
                }
            }

            /* Check incoming phase requests from external callers */
            if (!phase_change_needed) {
                phase_request_t req;
                if (xQueueReceive(xPhaseRequestQueue, &req, 0) == pdTRUE) {
                    if (req.reason == REASON_SAFETY) {
                        /* Safety request is immediate — skip intergreen */
                        command_all_red();
                        HAL_GPIO_WritePin(INHIBIT_LINE_GPIO_Port,
                                          INHIBIT_LINE_Pin, GPIO_PIN_SET);
                        s_ctx.next_phase_id = req.requested_phase_id;
                        s_ctx.current_state = STATE_FAULT_LOCKOUT;
                        s_ctx.fault_count++;
                        state_entry_tick = HAL_GetTick();
                        break;
                    }
                    if (s_ctx.phase_elapsed_ms >= pd->min_green_ms) {
                        s_ctx.next_phase_id = req.requested_phase_id;
                        phase_change_needed = true;
                    }
                }
            }

            if (phase_change_needed) {
                /* Transition to amber clearance — command amber on active approach */
                xEventGroupClearBits(xSystemEventGroup, SYSEVT_PHASE_ACTIVE);

                for (uint8_t a = 0; a < pd->num_approaches; a++) {
                    if (pd->approach_aspects[a] == ASPECT_GREEN) {
                        command_approach_aspect(a, ASPECT_AMBER,
                                                s_ctx.current_phase_id);
                    }
                }

                s_ctx.current_state = STATE_AMBER_CLEARANCE;
                state_entry_tick    = HAL_GetTick();
                log_phase_event(FAULT_NONE, FAULT_SEV_INFO,
                                (uint32_t)s_ctx.current_phase_id,
                                (uint32_t)STATE_AMBER_CLEARANCE);
            }
            break;
        }

        /* ==============================================================
         * STATE_AMBER_CLEARANCE
         * Amber running on previously-green approach.
         * Wait intergreen_before_ms then go to ALL_RED_CLEARANCE.
         * ============================================================== */
        case STATE_AMBER_CLEARANCE: {
            const phase_descriptor_t *pd = find_phase(s_ctx.current_phase_id);
            uint32_t intergreen = (pd != NULL) ? pd->intergreen_before_ms : 3000U;

            if ((now - state_entry_tick) >= intergreen) {
                command_all_red();
                s_ctx.current_state = STATE_ALL_RED_CLEARANCE;
                state_entry_tick    = HAL_GetTick();
                log_phase_event(FAULT_NONE, FAULT_SEV_INFO,
                                (uint32_t)s_ctx.current_phase_id,
                                (uint32_t)STATE_ALL_RED_CLEARANCE);
            }
            break;
        }

        /* ==============================================================
         * STATE_ALL_RED_CLEARANCE
         * All red dwell (all_red_after_ms) before next phase.
         * Transitions to STATE_ALL_RED for the next permissive cycle.
         * ============================================================== */
        case STATE_ALL_RED_CLEARANCE: {
            const phase_descriptor_t *pd = find_phase(s_ctx.current_phase_id);
            uint32_t all_red_after = (pd != NULL) ? pd->all_red_after_ms : 2000U;

            if ((now - state_entry_tick) >= all_red_after) {
                /* Move to ALL_RED which will request permissive for next_phase */
                permissive_requested = false;
                s_ctx.current_state  = STATE_ALL_RED;
                s_ctx.phase_start_tick = HAL_GetTick();
                state_entry_tick     = HAL_GetTick();
                log_phase_event(FAULT_NONE, FAULT_SEV_INFO,
                                (uint32_t)s_ctx.next_phase_id,
                                (uint32_t)STATE_ALL_RED);
            }
            break;
        }

        /* ==============================================================
         * STATE_FAULT_LOCKOUT
         * Inhibit asserted.  All approaches commanded dark via CAN.
         * Remains here until SYSEVT_FAULT_ACTIVE is cleared by
         * FaultLogTask and the external caller calls set_mode(NORMAL).
         * ============================================================== */
        case STATE_FAULT_LOCKOUT: {
            /* Maintain inhibit */
            HAL_GPIO_WritePin(INHIBIT_LINE_GPIO_Port, INHIBIT_LINE_Pin, GPIO_PIN_SET);

            /* Periodically reissue dark command in case LSOs missed it */
            static uint32_t last_dark_cmd = 0;
            if ((now - last_dark_cmd) >= 500U) {
                command_all_dark();
                last_dark_cmd = now;
            }

            /* Check if fault has been externally cleared */
            EventBits_t bits = xEventGroupGetBits(xSystemEventGroup);
            if (!(bits & SYSEVT_FAULT_ACTIVE) && s_mode == TE_MODE_NORMAL) {
                /* Return to ALL_RED to begin fresh intergreen cycle */
                command_all_red();
                HAL_GPIO_WritePin(INHIBIT_LINE_GPIO_Port, INHIBIT_LINE_Pin, GPIO_PIN_RESET);
                permissive_requested   = false;
                s_ctx.current_state    = STATE_ALL_RED;
                s_ctx.next_phase_id    = select_next_phase();
                s_ctx.phase_start_tick = HAL_GetTick();
                state_entry_tick       = HAL_GetTick();
                log_phase_event(FAULT_NONE, FAULT_SEV_INFO,
                                (uint32_t)STATE_FAULT_LOCKOUT, (uint32_t)STATE_ALL_RED);
            }
            break;
        }

        /* ==============================================================
         * STATE_MANUAL_OVERRIDE
         * Phase selection driven externally via xPhaseCommandQueue.
         * ============================================================== */
        case STATE_MANUAL_OVERRIDE: {
            phase_request_t req;
            if (xQueueReceive(xPhaseRequestQueue, &req, 0) == pdTRUE) {
                permissive_result_t result =
                    safety_comm_request_permissive(req.requested_phase_id);
                if (result == PERMISSIVE_GRANTED) {
                    HAL_GPIO_WritePin(INHIBIT_LINE_GPIO_Port,
                                      INHIBIT_LINE_Pin, GPIO_PIN_RESET);
                    s_ctx.current_phase_id = req.requested_phase_id;
                    s_ctx.phase_start_tick = HAL_GetTick();
                    const phase_descriptor_t *pd =
                        find_phase(s_ctx.current_phase_id);
                    if (pd != NULL) {
                        for (uint8_t a = 0; a < pd->num_approaches; a++) {
                            command_approach_aspect(a, pd->approach_aspects[a],
                                                    s_ctx.current_phase_id);
                        }
                    }
                    xEventGroupSetBits(xSystemEventGroup, SYSEVT_PHASE_ACTIVE);
                }
            }

            /* Exit manual override when mode is reset to NORMAL */
            if (s_mode == TE_MODE_NORMAL) {
                command_all_red();
                permissive_requested = false;
                s_ctx.next_phase_id  = select_next_phase();
                s_ctx.current_state  = STATE_ALL_RED;
                state_entry_tick     = HAL_GetTick();
                xEventGroupClearBits(xSystemEventGroup, SYSEVT_PHASE_ACTIVE);
            }
            break;
        }

        /* ==============================================================
         * STATE_BLACKOUT
         * All outputs dark.  Inhibit de-asserted (LSOs handle darkness).
         * Exits when mode returns to NORMAL.
         * ============================================================== */
        case STATE_BLACKOUT: {
            static uint32_t last_dark_refresh = 0;
            if ((now - last_dark_refresh) >= 1000U) {
                command_all_dark();
                last_dark_refresh = now;
            }

            if (s_mode == TE_MODE_NORMAL) {
                command_all_red();
                HAL_GPIO_WritePin(INHIBIT_LINE_GPIO_Port,
                                  INHIBIT_LINE_Pin, GPIO_PIN_RESET);
                permissive_requested = false;
                s_ctx.next_phase_id  = select_next_phase();
                s_ctx.current_state  = STATE_ALL_RED;
                state_entry_tick     = HAL_GetTick();
            }
            break;
        }

        default:
            /* Should never happen */
            s_ctx.current_state = STATE_FAULT_LOCKOUT;
            break;

        } /* end switch */

        /* ---------------------------------------------------------------
         * Sleep until next tick
         * --------------------------------------------------------------- */
        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

/* ============================================================================
 * traffic_engine_request_phase
 * ============================================================================ */
BaseType_t traffic_engine_request_phase(const phase_request_t *req)
{
    if (req == NULL || xPhaseRequestQueue == NULL) {
        return pdFALSE;
    }
    return xQueueSend(xPhaseRequestQueue, req, 0);
}

/* ============================================================================
 * traffic_engine_get_state
 * ============================================================================ */
traffic_state_t traffic_engine_get_state(void)
{
    traffic_state_t state;
    if (xTEMutex != NULL && xSemaphoreTake(xTEMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        state = s_ctx.current_state;
        xSemaphoreGive(xTEMutex);
    } else {
        state = s_ctx.current_state;  /* best-effort read if mutex unavailable */
    }
    return state;
}

/* ============================================================================
 * traffic_engine_set_mode
 * ============================================================================ */
void traffic_engine_set_mode(traffic_engine_mode_t mode)
{
    s_mode = mode;  /* volatile write — atomic on Cortex-M7 for enum-sized int */
}

/* ============================================================================
 * traffic_engine_load_plan
 * ============================================================================ */
bool traffic_engine_load_plan(const phase_plan_t *plan)
{
    if (plan == NULL) {
        return false;
    }

    if (!validate_plan_crc(plan)) {
        log_phase_event(FAULT_PHASE_PLAN_INVALID, FAULT_SEV_WARNING,
                        plan->plan_version, 0);
        return false;
    }

    if (xTEMutex != NULL && xSemaphoreTake(xTEMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        gpActivePhasePlan      = plan;
        s_ctx.active_plan      = plan;
        /* If currently in ALL_RED, update next phase to the plan's start */
        if (s_ctx.current_state == STATE_ALL_RED ||
            s_ctx.current_state == STATE_ALL_RED_CLEARANCE) {
            s_ctx.next_phase_id = plan->start_phase_id;
        }
        xSemaphoreGive(xTEMutex);
    } else {
        gpActivePhasePlan = plan;
        s_ctx.active_plan = plan;
    }

    return true;
}

/* ============================================================================
 * Private helpers
 * ============================================================================ */

/**
 * @brief  Select the next phase using round-robin or demand-based logic.
 *
 *         If the current plan is valid and the current phase has a
 *         non-zero next_phase_default, that is used.  Otherwise we
 *         increment to the next phase ID, wrapping at num_phases.
 */
static uint8_t select_next_phase(void)
{
    const phase_plan_t *plan = gpActivePhasePlan;
    if (plan == NULL || plan->num_phases == 0U) {
        return 1U;   /* fallback: phase 1 */
    }

    const phase_descriptor_t *current = find_phase(s_ctx.current_phase_id);

    /* Demand-based: find the approach with the oldest pending demand */
    if (current != NULL && current->demand_enabled && s_ctx.demand_flags != 0U) {
        for (uint8_t b = 0; b < plan->num_phases; b++) {
            uint8_t pid = plan->phases[b].phase_id;
            /* Simplified: pick the plan phase that serves the pending demand */
            if (s_ctx.demand_flags & (1U << b)) {
                return pid;
            }
        }
    }

    /* Explicit successor from plan */
    if (current != NULL && current->next_phase_default != 0U) {
        return current->next_phase_default;
    }

    /* Round-robin fallback */
    if (s_ctx.current_phase_id == 0U) {
        return plan->phases[0].phase_id;
    }
    for (uint8_t i = 0; i < plan->num_phases; i++) {
        if (plan->phases[i].phase_id == s_ctx.current_phase_id) {
            uint8_t next_idx = (uint8_t)((i + 1U) % plan->num_phases);
            return plan->phases[next_idx].phase_id;
        }
    }

    return plan->phases[0].phase_id;  /* wrap-around fallback */
}

/**
 * @brief  Send a CAN FD frame commanding an approach to a specific aspect.
 *
 *         CAN ID = CAN_LSO_CMD_BASE_ID + approach_id
 *         Data layout:
 *           [0] phase_id
 *           [1] approach_id
 *           [2] aspect (signal_aspect_t)
 *           [3] duration high byte (ms)
 *           [4] duration low byte (ms)
 *           [5..7] reserved
 */
static HAL_StatusTypeDef command_approach_aspect(uint8_t approach_id,
                                                  uint8_t aspect,
                                                  uint8_t phase_id)
{
    FDCAN_TxHeaderTypeDef tx_hdr = {
        .Identifier          = CAN_LSO_CMD_BASE_ID + (uint32_t)approach_id,
        .IdType              = FDCAN_STANDARD_ID,
        .TxFrameType         = FDCAN_DATA_FRAME,
        .DataLength          = FDCAN_DLC_BYTES_8,
        .ErrorStateIndicator = FDCAN_ESI_ACTIVE,
        .BitRateSwitch       = FDCAN_BRS_OFF,
        .FDFormat            = FDCAN_CLASSIC_CAN,
        .TxEventFifoControl  = FDCAN_NO_TX_EVENTS,
        .MessageMarker       = 0,
    };

    uint8_t data[8] = {
        phase_id,
        approach_id,
        aspect,
        0,  /* duration hi — 0 means "maintain until next command" */
        0,  /* duration lo */
        0, 0, 0
    };

    return HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &tx_hdr, data);
}

/**
 * @brief  Broadcast an all-red command to every approach via CAN.
 *         Uses the broadcast CAN ID so all LSOs react in one frame.
 */
static void command_all_red(void)
{
    FDCAN_TxHeaderTypeDef tx_hdr = {
        .Identifier          = CAN_LSO_BROADCAST_ID,
        .IdType              = FDCAN_STANDARD_ID,
        .TxFrameType         = FDCAN_DATA_FRAME,
        .DataLength          = FDCAN_DLC_BYTES_8,
        .ErrorStateIndicator = FDCAN_ESI_ACTIVE,
        .BitRateSwitch       = FDCAN_BRS_OFF,
        .FDFormat            = FDCAN_CLASSIC_CAN,
        .TxEventFifoControl  = FDCAN_NO_TX_EVENTS,
        .MessageMarker       = 0,
    };
    uint8_t data[8] = { 0, 0xFF, ASPECT_RED, 0, 0, 0, 0, 0 };
    HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &tx_hdr, data);
}

/**
 * @brief  Broadcast an all-dark (off) command and assert the inhibit line.
 */
static void command_all_dark(void)
{
    HAL_GPIO_WritePin(INHIBIT_LINE_GPIO_Port, INHIBIT_LINE_Pin, GPIO_PIN_SET);

    FDCAN_TxHeaderTypeDef tx_hdr = {
        .Identifier          = CAN_LSO_BROADCAST_ID,
        .IdType              = FDCAN_STANDARD_ID,
        .TxFrameType         = FDCAN_DATA_FRAME,
        .DataLength          = FDCAN_DLC_BYTES_8,
        .ErrorStateIndicator = FDCAN_ESI_ACTIVE,
        .BitRateSwitch       = FDCAN_BRS_OFF,
        .FDFormat            = FDCAN_CLASSIC_CAN,
        .TxEventFifoControl  = FDCAN_NO_TX_EVENTS,
        .MessageMarker       = 0,
    };
    uint8_t data[8] = { 0, 0xFF, ASPECT_OFF, 0, 0, 0, 0, 0 };
    HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &tx_hdr, data);
}

/**
 * @brief  Post a fault event to xFaultQueue for FaultLogTask to persist.
 *         Uses FAULT_NONE code to encode informational state transitions.
 */
static void log_phase_event(fault_code_t     code,
                             fault_severity_t sev,
                             uint32_t         ctx0,
                             uint32_t         ctx1)
{
    fault_event_t evt = {
        .code         = code,
        .severity     = sev,
        .source       = FAULT_SRC_TRAFFIC_ENGINE,
        .timestamp_ms = HAL_GetTick(),
        .context      = { ctx0, ctx1 }
    };
    xQueueSend(xFaultQueue, &evt, 0);  /* non-blocking: drop if queue full */
}

/**
 * @brief  Validate the CRC-16 of a phase plan.
 *
 * @return true if CRC matches.
 */
static bool validate_plan_crc(const phase_plan_t *plan)
{
    size_t   check_len = offsetof(phase_plan_t, crc16);
    uint16_t computed  = crc16_ccitt((const uint8_t *)plan, check_len);
    return (computed == plan->crc16);
}

/**
 * @brief  Find a phase descriptor by phase_id within the active plan.
 *
 * @return Pointer to the descriptor, or NULL if not found.
 */
static const phase_descriptor_t *find_phase(uint8_t phase_id)
{
    const phase_plan_t *plan = gpActivePhasePlan;
    if (plan == NULL || phase_id == 0U) {
        return NULL;
    }
    for (uint8_t i = 0; i < plan->num_phases; i++) {
        if (plan->phases[i].phase_id == phase_id) {
            return &plan->phases[i];
        }
    }
    return NULL;
}
