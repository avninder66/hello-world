/**
 * @file    traffic_engine.h
 * @brief   LCU-100 Traffic Engine — state machine public interface
 *
 * The traffic engine is the core decision-making component of the LCU-100.
 * It executes a phase plan loaded from FRAM or received from the TMC,
 * manages intergreen timers, coordinates with the safety supervisor for
 * permissive grants, and drives phase commands to LSO modules over CAN.
 *
 * All public functions are thread-safe: they either operate on immutable data
 * or use the internal mutex xTEMutex to serialise access to the context struct.
 */

#ifndef __TRAFFIC_ENGINE_H
#define __TRAFFIC_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "phase_plan.h"
#include "fault_codes.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"
#include "semphr.h"

/* ============================================================================
 * Traffic engine state enumeration
 * ============================================================================ */
typedef enum {
    /**
     * @brief  Waiting for safety supervisor and CAN bus to report healthy.
     *         All outputs remain at whatever the GPIO default is (inhibit asserted).
     */
    STATE_STARTUP           = 0,

    /**
     * @brief  All approaches commanded RED.  Used as a stable, safe dwell
     *         between phase transitions and after fault clearance.
     */
    STATE_ALL_RED           = 1,

    /**
     * @brief  A green (or green-equivalent) phase is running on the active
     *         approach.  Phase timer and demand flags are monitored here.
     */
    STATE_PHASE_ACTIVE      = 2,

    /**
     * @brief  Amber clearance interval.  The previously-green approach shows
     *         amber for intergreen_before_ms before all-red is entered.
     */
    STATE_AMBER_CLEARANCE   = 3,

    /**
     * @brief  All-red dwell after amber clearance (all_red_after_ms) before
     *         the next phase transitions to PHASE_ACTIVE.
     */
    STATE_ALL_RED_CLEARANCE = 4,

    /**
     * @brief  Active fault detected.  Inhibit asserted, all outputs commanded
     *         dark via CAN.  Remains here until fault is cleared and a manual
     *         reset is applied or the TMC clears the fault remotely.
     */
    STATE_FAULT_LOCKOUT     = 5,

    /**
     * @brief  Operator manual override active.  Phase selection driven by
     *         front-panel controls or TMC manual command rather than the plan.
     */
    STATE_MANUAL_OVERRIDE   = 6,

    /**
     * @brief  All signal heads commanded off (dark).  Used during power-saving
     *         periods or when instructed by TMC.
     */
    STATE_BLACKOUT          = 7,
} traffic_state_t;

/* ============================================================================
 * Phase change request
 * ============================================================================ */

/** Why a phase change is being requested */
typedef enum {
    REASON_TIMER    = 0,    /**< max_green_ms expired (scheduler-driven)       */
    REASON_DEMAND   = 1,    /**< Vehicle/pedestrian demand registered           */
    REASON_MANUAL   = 2,    /**< Operator or TMC explicit command               */
    REASON_SAFETY   = 3,    /**< Safety supervisor forced transition            */
} phase_change_reason_t;

typedef struct {
    uint8_t               requested_phase_id;
    phase_change_reason_t reason;
} phase_request_t;

/* ============================================================================
 * Traffic engine runtime context
 * ============================================================================ */
typedef struct {
    /** Current FSM state */
    traffic_state_t current_state;

    /** Phase ID currently running (1-based; 0 = none) */
    uint8_t current_phase_id;

    /** Phase ID that will run after the next intergreen sequence */
    uint8_t next_phase_id;

    /** HAL_GetTick() snapshot when current phase started */
    uint32_t phase_start_tick;

    /** Elapsed time (ms) since the current phase began — updated each loop */
    uint32_t phase_elapsed_ms;

    /**
     * @brief  Demand bitmask: bit N set means approach N+1 has registered
     *         a demand (vehicle detector or push-button input) since the
     *         last time it was serviced.
     */
    uint8_t demand_flags;

    /** Running count of faults that entered STATE_FAULT_LOCKOUT */
    uint32_t fault_count;

    /** Pointer to the active phase plan (NULL until loaded) */
    const phase_plan_t *active_plan;
} traffic_engine_context_t;

/* ============================================================================
 * Engine operating mode
 * ============================================================================ */
typedef enum {
    TE_MODE_NORMAL          = 0,
    TE_MODE_MANUAL_OVERRIDE = 1,
    TE_MODE_BLACKOUT        = 2,
    TE_MODE_FLASH_AMBER     = 3,    /**< All approaches flash amber (low-traffic) */
} traffic_engine_mode_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * @brief  Initialise the traffic engine.
 *
 *         Creates xTEMutex and any engine-private FreeRTOS objects.
 *         Must be called before vTaskStartScheduler().
 *
 * @return true on success, false if a resource allocation failed.
 */
bool traffic_engine_init(void);

/**
 * @brief  FreeRTOS task entry point — runs the 50 ms state-machine loop.
 *
 *         Registered with xTaskCreate() in main.c as "TrafficEngine".
 *
 * @param pvParameters  Unused; pass NULL.
 */
void traffic_engine_task(void *pvParameters);

/**
 * @brief  Request a phase transition from any task context.
 *
 *         Thread-safe.  The request is queued; the engine processes it on the
 *         next 50 ms tick.
 *
 * @param[in] req  Phase request descriptor.
 * @return pdTRUE if the request was enqueued, pdFALSE if the queue was full.
 */
BaseType_t traffic_engine_request_phase(const phase_request_t *req);

/**
 * @brief  Read the current traffic engine state (thread-safe snapshot).
 *
 * @return Current traffic_state_t value.
 */
traffic_state_t traffic_engine_get_state(void);

/**
 * @brief  Change the engine operating mode.
 *
 *         Thread-safe.  Mode changes take effect on the next 50 ms tick.
 *         Transitioning to TE_MODE_NORMAL will resume normal plan execution.
 *
 * @param[in] mode  Target operating mode.
 */
void traffic_engine_set_mode(traffic_engine_mode_t mode);

/**
 * @brief  Load a new phase plan into the engine.
 *
 *         Thread-safe.  Validates the plan CRC before accepting it.  If
 *         the engine is currently running a phase the transition to the new
 *         plan happens at the next ALL_RED state.
 *
 * @param[in] plan  Pointer to a fully-populated phase_plan_t.
 *                  The plan must remain valid for the engine's lifetime
 *                  (stored by pointer, not copied).
 * @return true if the plan CRC is valid and the plan was accepted.
 */
bool traffic_engine_load_plan(const phase_plan_t *plan);

/* ============================================================================
 * Extern: active phase plan (set by traffic_engine_load_plan, read-only after)
 * ============================================================================ */
extern const phase_plan_t *gpActivePhasePlan;

#ifdef __cplusplus
}
#endif

#endif /* __TRAFFIC_ENGINE_H */
