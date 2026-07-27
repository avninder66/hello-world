/**
 * @file    phase_plan.h
 * @brief   Traffic phase plan configuration structures for the Lumina LCU-100.
 *
 * Defines the data model for traffic signal phase plans, approach
 * configurations, and operating modes.  Phase plan instances are stored in
 * internal flash and validated at startup using the embedded CRC-32.
 *
 * UK TOPAS compliance notes:
 *   - All-red periods between conflicting phases: minimum 2 s (TOPAS §4.3).
 *   - Amber clearance period: 3 s (TOPAS §4.2).
 *   - Minimum green time for vehicles: 7 s (TOPAS §4.4).
 *   - Minimum green time at pedestrian crossings: 4 s push-button, 7 s
 *     signal-controlled (TOPAS §5.1).
 *   - Maximum phase time in demand-controlled mode: site-specific, default 90 s.
 *
 * Predefined plan:
 *   Plan 0  — 2-way shuttle (single-lane contraflow, two approach traffic streams).
 *
 * Copyright (c) Lumina Traffic Systems Ltd.  All rights reserved.
 * SPDX-License-Identifier: Proprietary
 */

#ifndef PHASE_PLAN_H
#define PHASE_PLAN_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Capacity limits
 * ========================================================================= */

/** Maximum number of traffic phases in a single plan. */
#define MAX_PHASES                  16U

/** Maximum number of vehicular / pedestrian approaches in a plan. */
#define MAX_APPROACHES              4U

/** Channels per approach: [0]=Red, [1]=Amber, [2]=Green. */
#define MAX_CHANNELS_PER_APPROACH   3U

/** Maximum length of a phase name including NUL terminator. */
#define PHASE_NAME_LEN              16U

/** Maximum length of an approach name including NUL terminator. */
#define APPROACH_NAME_LEN           16U

/** Maximum length of a plan name including NUL terminator. */
#define PLAN_NAME_LEN               32U

/** Sentinel value meaning "channel not used" in approach config. */
#define CHANNEL_UNUSED              0xFFU

/* =========================================================================
 * UK TOPAS timing constants (milliseconds)
 * ========================================================================= */

/** Amber clearance period — TOPAS §4.2. */
#define TOPAS_AMBER_PERIOD_MS           3000U

/** Minimum all-red between conflicting phases — TOPAS §4.3. */
#define TOPAS_ALL_RED_MIN_MS            2000U

/** Minimum vehicle green time — TOPAS §4.4. */
#define TOPAS_MIN_GREEN_VEHICLE_MS      7000U

/** Minimum pedestrian green time (push-button site) — TOPAS §5.1. */
#define TOPAS_MIN_GREEN_PED_MS          4000U

/** Maximum red time before a mandatory green is forced. */
#define TOPAS_MAX_RED_MS               120000U

/** Default maximum phase duration in demand-controlled mode. */
#define TOPAS_DEFAULT_MAX_PHASE_MS      90000U

/* =========================================================================
 * Signal aspect enumeration
 * ========================================================================= */

/**
 * @brief Traffic signal aspect (combination of lamp states on one approach).
 *
 * Values map directly to combinations of red, amber, and green channels.
 * The LSO channel state for each lamp within an aspect is determined by the
 * LCU firmware using the approach_config_t channel assignments.
 */
typedef enum
{
    ASPECT_OFF              = 0U,   /**< All lamps off (blackout / maintenance)      */
    ASPECT_RED              = 1U,   /**< Red steady                                  */
    ASPECT_AMBER            = 2U,   /**< Amber steady (caution / clearance)          */
    ASPECT_GREEN            = 3U,   /**< Green steady (go)                           */
    ASPECT_RED_AMBER        = 4U,   /**< Red + Amber together (UK prepare-to-go)     */
    ASPECT_FLASHING_AMBER   = 5U,   /**< Flashing amber (give way)                   */
    ASPECT_FLASHING_GREEN   = 6U,   /**< Flashing green (pedestrian countdown)       */
    ASPECT_PED_RED          = 7U,   /**< Pedestrian red man                          */
    ASPECT_PED_GREEN        = 8U,   /**< Pedestrian green man                        */
} signal_aspect_t;

/* =========================================================================
 * Operating mode enumeration
 * ========================================================================= */

typedef enum
{
    /**
     * Fixed-time control: phases run for their configured duration_ms
     * unconditionally, ignoring detector inputs.
     */
    MODE_FIXED_TIME         = 0U,

    /**
     * Demand-controlled: phases extend up to max_duration_ms while demand
     * is present, terminate at min_duration_ms if no demand detected.
     */
    MODE_DEMAND_CONTROLLED  = 1U,

    /**
     * Manual control: the operator selects phases via keypad or remote
     * command; automatic sequencing is disabled.
     */
    MODE_MANUAL             = 2U,

    /**
     * All-red: all approaches show red; used during fault recovery or
     * operator override.  Corresponds to LUMINA_SAFETY_STATE_ALL_RED.
     */
    MODE_ALL_RED            = 3U,

    /**
     * Blackout: all lamp outputs are de-energised.  Used during initial
     * deployment, low-battery shutdown, or external command.
     */
    MODE_BLACKOUT           = 4U,
} operating_mode_t;

/* =========================================================================
 * Approach configuration
 * ========================================================================= */

/**
 * @brief Configuration for a single traffic or pedestrian approach.
 *
 * Maps approach signals to physical LSO-100 channel numbers (0-based, 0–11).
 * Set unused fields to CHANNEL_UNUSED (0xFF).
 */
typedef struct
{
    uint8_t     approach_id;                        /**< Unique approach index 0..MAX_APPROACHES-1  */
    char        name[APPROACH_NAME_LEN];            /**< Human-readable name, e.g. "NORTH"          */

    /** Vehicular signal channels. */
    uint8_t     red_channel;                        /**< LSO channel for red lamp                   */
    uint8_t     amber_channel;                      /**< LSO channel for amber lamp                 */
    uint8_t     green_channel;                      /**< LSO channel for green lamp                 */

    /** Pedestrian signal channels (CHANNEL_UNUSED if no ped signals fitted). */
    uint8_t     ped_red_channel;                    /**< LSO channel for pedestrian red man         */
    uint8_t     ped_green_channel;                  /**< LSO channel for pedestrian green man       */

    /**
     * Push-button fitted flag.  When true, the LPI-100 pedestrian event for
     * this approach_id will register a demand and extend the green phase.
     */
    uint8_t     push_button_fitted;                 /**< Non-zero = push button present             */

    /**
     * Approach type determines which timing constraints apply.
     * 0 = vehicle, 1 = pedestrian crossing, 2 = cycle lane.
     */
    uint8_t     approach_type;                      /**< 0=vehicle, 1=pedestrian, 2=cycle           */

    uint8_t     reserved[2];                        /**< Alignment padding, must be 0x00            */
} approach_config_t;

/* =========================================================================
 * Phase definition
 * ========================================================================= */

/**
 * @brief Definition of a single traffic signal phase.
 *
 * A phase specifies the signal aspect for every approach simultaneously and
 * the timing constraints that govern how long the phase runs.
 */
typedef struct
{
    uint8_t     phase_id;                           /**< Phase index 0..MAX_PHASES-1                */
    char        name[PHASE_NAME_LEN];               /**< Human-readable name, e.g. "PHASE_A"        */

    /**
     * Nominal duration in milliseconds.
     * Set to 0 in demand-controlled mode; the controller will use
     * min_duration_ms and extend up to max_duration_ms based on demand.
     */
    uint32_t    duration_ms;

    /** Minimum time the phase must run regardless of demand. */
    uint32_t    min_duration_ms;

    /** Maximum time the phase may run before forced termination. */
    uint32_t    max_duration_ms;

    /**
     * Signal aspect for each approach during this phase.
     * Index matches approach_config_t.approach_id.
     * Approaches not active in this phase should be set to ASPECT_RED.
     */
    signal_aspect_t aspect[MAX_APPROACHES];

    /**
     * All-red period that must elapse BEFORE this phase becomes active.
     * The safety supervisor enforces this via its intergreen timer.
     * Must be >= TOPAS_ALL_RED_MIN_MS when conflicting phases are adjacent.
     */
    uint32_t    intergreen_before_ms;

    /**
     * All-red period that must elapse AFTER this phase ends and before any
     * conflicting phase may start.  Must be >= TOPAS_ALL_RED_MIN_MS.
     */
    uint32_t    all_red_after_ms;

    /**
     * Demand-controlled extension step in milliseconds.  When demand is
     * registered during a phase, the phase is extended by this amount (clamped
     * to max_duration_ms).  0 = no extension (fixed-time even in DC mode).
     */
    uint32_t    demand_extension_ms;

    /**
     * Pedestrian demand flag: if non-zero, pedestrian button actuations for
     * the approach(es) active in this phase will be registered as demand.
     */
    uint8_t     ped_demand_enabled;

    /**
     * Phase sequence: index of the next phase to activate after this phase
     * completes in fixed-time mode.  0xFF = return to phase_id 0.
     */
    uint8_t     next_phase_id;

    uint8_t     reserved[2];                        /**< Alignment padding, must be 0x00            */
} phase_t;

/* =========================================================================
 * Phase plan
 * ========================================================================= */

/**
 * @brief A complete traffic signal phase plan.
 *
 * Stored in internal NVM flash.  The config_crc32 field covers all bytes
 * of the struct from plan_id through approaches[] inclusive (i.e. all fields
 * except config_crc32 itself) and is validated at startup.
 */
typedef struct
{
    uint8_t             plan_id;                    /**< Plan index 0..7                            */
    char                name[PLAN_NAME_LEN];        /**< Human-readable plan name                   */
    uint8_t             num_phases;                 /**< Number of valid entries in phases[]         */
    uint8_t             approach_count;             /**< Number of valid entries in approaches[]     */
    uint8_t             default_mode;               /**< operating_mode_t to use on power-on        */
    uint8_t             reserved[5];                /**< Alignment padding, must be 0x00            */
    phase_t             phases[MAX_PHASES];         /**< Phase definitions                          */
    approach_config_t   approaches[MAX_APPROACHES]; /**< Approach channel maps                      */
    uint32_t            config_crc32;               /**< CRC-32 of all preceding fields             */
} phase_plan_t;

/* =========================================================================
 * Predefined plan: Plan 0 — 2-way shuttle (single-lane contraflow)
 *
 * Topology:
 *   Approach 0  "NORTH"  — northbound vehicular + pedestrian, channels 0/1/2 (R/A/G),
 *                          ped channels 3/4 (red man / green man)
 *   Approach 1  "SOUTH"  — southbound vehicular + pedestrian, channels 5/6/7 (R/A/G),
 *                          ped channels 8/9 (red man / green man)
 *   Approaches 2,3 unused.
 *
 * Phase sequence (fixed-time default, TOPAS-compliant):
 *
 *   PHASE A — North GREEN
 *     North: Green  South: Red
 *     Duration: 30 s (min 7 s, max 90 s)
 *     Intergreen before: 2 s all-red
 *     All-red after: 0 (amber clearance covers it)
 *
 *   PHASE A-AMBER — North AMBER clearance
 *     North: Amber  South: Red
 *     Duration: 3 s (fixed, demand extension = 0)
 *
 *   PHASE ALL-RED-AB — All red transition A→B
 *     Duration: 2 s
 *
 *   PHASE B — South GREEN
 *     North: Red  South: Green
 *     Duration: 30 s (min 7 s, max 90 s)
 *
 *   PHASE B-AMBER — South AMBER clearance
 *     Duration: 3 s (fixed)
 *
 *   PHASE ALL-RED-BA — All red transition B→A
 *     Duration: 2 s
 *
 * Total minimum cycle: 7+3+2+7+3+2 = 24 s
 * Nominal fixed-time cycle: 30+3+2+30+3+2 = 70 s
 * ========================================================================= */

/**
 * @brief Extern declaration for the Plan 0 (2-way shuttle) constant definition.
 *
 * Defined in firmware/common/config/phase_plan.c.
 */
extern const phase_plan_t phase_plan_shuttle_2way;

/* =========================================================================
 * Inline initialiser macros for Plan 0
 *
 * These macros expand to designated-initialiser expressions so that the plan
 * can be placed in flash (.rodata) with a const definition in phase_plan.c.
 * ========================================================================= */

/** Approach 0: Northbound vehicle + pedestrian. */
#define PLAN0_APPROACH_NORTH \
{ \
    .approach_id        = 0U, \
    .name               = "NORTH", \
    .red_channel        = 0U, \
    .amber_channel      = 1U, \
    .green_channel      = 2U, \
    .ped_red_channel    = 3U, \
    .ped_green_channel  = 4U, \
    .push_button_fitted = 1U, \
    .approach_type      = 1U, \
    .reserved           = {0U, 0U} \
}

/** Approach 1: Southbound vehicle + pedestrian. */
#define PLAN0_APPROACH_SOUTH \
{ \
    .approach_id        = 1U, \
    .name               = "SOUTH", \
    .red_channel        = 5U, \
    .amber_channel      = 6U, \
    .green_channel      = 7U, \
    .ped_red_channel    = 8U, \
    .ped_green_channel  = 9U, \
    .push_button_fitted = 1U, \
    .approach_type      = 1U, \
    .reserved           = {0U, 0U} \
}

/** Phase A: North GREEN, South RED. */
#define PLAN0_PHASE_A \
{ \
    .phase_id               = 0U, \
    .name                   = "PHASE_A_GREEN", \
    .duration_ms            = 30000U, \
    .min_duration_ms        = TOPAS_MIN_GREEN_VEHICLE_MS, \
    .max_duration_ms        = TOPAS_DEFAULT_MAX_PHASE_MS, \
    .aspect                 = { ASPECT_GREEN, ASPECT_RED, ASPECT_RED, ASPECT_RED }, \
    .intergreen_before_ms   = TOPAS_ALL_RED_MIN_MS, \
    .all_red_after_ms       = 0U, \
    .demand_extension_ms    = 5000U, \
    .ped_demand_enabled     = 1U, \
    .next_phase_id          = 1U, \
    .reserved               = {0U, 0U} \
}

/** Phase A-Amber: North AMBER clearance, South RED. */
#define PLAN0_PHASE_A_AMBER \
{ \
    .phase_id               = 1U, \
    .name                   = "PHASE_A_AMBER", \
    .duration_ms            = TOPAS_AMBER_PERIOD_MS, \
    .min_duration_ms        = TOPAS_AMBER_PERIOD_MS, \
    .max_duration_ms        = TOPAS_AMBER_PERIOD_MS, \
    .aspect                 = { ASPECT_AMBER, ASPECT_RED, ASPECT_RED, ASPECT_RED }, \
    .intergreen_before_ms   = 0U, \
    .all_red_after_ms       = 0U, \
    .demand_extension_ms    = 0U, \
    .ped_demand_enabled     = 0U, \
    .next_phase_id          = 2U, \
    .reserved               = {0U, 0U} \
}

/** Phase ALL-RED-AB: transition from A to B. */
#define PLAN0_PHASE_ALL_RED_AB \
{ \
    .phase_id               = 2U, \
    .name                   = "ALL_RED_AB", \
    .duration_ms            = TOPAS_ALL_RED_MIN_MS, \
    .min_duration_ms        = TOPAS_ALL_RED_MIN_MS, \
    .max_duration_ms        = TOPAS_ALL_RED_MIN_MS, \
    .aspect                 = { ASPECT_RED, ASPECT_RED, ASPECT_RED, ASPECT_RED }, \
    .intergreen_before_ms   = 0U, \
    .all_red_after_ms       = 0U, \
    .demand_extension_ms    = 0U, \
    .ped_demand_enabled     = 0U, \
    .next_phase_id          = 3U, \
    .reserved               = {0U, 0U} \
}

/** Phase B: South GREEN, North RED. */
#define PLAN0_PHASE_B \
{ \
    .phase_id               = 3U, \
    .name                   = "PHASE_B_GREEN", \
    .duration_ms            = 30000U, \
    .min_duration_ms        = TOPAS_MIN_GREEN_VEHICLE_MS, \
    .max_duration_ms        = TOPAS_DEFAULT_MAX_PHASE_MS, \
    .aspect                 = { ASPECT_RED, ASPECT_GREEN, ASPECT_RED, ASPECT_RED }, \
    .intergreen_before_ms   = TOPAS_ALL_RED_MIN_MS, \
    .all_red_after_ms       = 0U, \
    .demand_extension_ms    = 5000U, \
    .ped_demand_enabled     = 1U, \
    .next_phase_id          = 4U, \
    .reserved               = {0U, 0U} \
}

/** Phase B-Amber: South AMBER clearance, North RED. */
#define PLAN0_PHASE_B_AMBER \
{ \
    .phase_id               = 4U, \
    .name                   = "PHASE_B_AMBER", \
    .duration_ms            = TOPAS_AMBER_PERIOD_MS, \
    .min_duration_ms        = TOPAS_AMBER_PERIOD_MS, \
    .max_duration_ms        = TOPAS_AMBER_PERIOD_MS, \
    .aspect                 = { ASPECT_RED, ASPECT_AMBER, ASPECT_RED, ASPECT_RED }, \
    .intergreen_before_ms   = 0U, \
    .all_red_after_ms       = 0U, \
    .demand_extension_ms    = 0U, \
    .ped_demand_enabled     = 0U, \
    .next_phase_id          = 5U, \
    .reserved               = {0U, 0U} \
}

/** Phase ALL-RED-BA: transition from B back to A. */
#define PLAN0_PHASE_ALL_RED_BA \
{ \
    .phase_id               = 5U, \
    .name                   = "ALL_RED_BA", \
    .duration_ms            = TOPAS_ALL_RED_MIN_MS, \
    .min_duration_ms        = TOPAS_ALL_RED_MIN_MS, \
    .max_duration_ms        = TOPAS_ALL_RED_MIN_MS, \
    .aspect                 = { ASPECT_RED, ASPECT_RED, ASPECT_RED, ASPECT_RED }, \
    .intergreen_before_ms   = 0U, \
    .all_red_after_ms       = 0U, \
    .demand_extension_ms    = 0U, \
    .ped_demand_enabled     = 0U, \
    .next_phase_id          = 0U, \
    .reserved               = {0U, 0U} \
}

/* =========================================================================
 * Phase plan validation helper
 * ========================================================================= */

/**
 * @brief Return true if a phase_plan_t pointer refers to a structurally valid plan.
 *
 * Checks that:
 *  - num_phases is non-zero and <= MAX_PHASES
 *  - approach_count is non-zero and <= MAX_APPROACHES
 *  - every phase min_duration_ms <= max_duration_ms
 *  - every phase intergreen_before_ms >= TOPAS_ALL_RED_MIN_MS where the
 *    preceding phase has conflicting aspects (caller is responsible for the
 *    full conflict check; this function only validates timing sanity)
 *
 * Does NOT verify config_crc32; call phase_plan_crc_valid() for that.
 *
 * @param plan  Pointer to the plan to validate.
 * @return      Non-zero if the plan passes basic sanity checks.
 */
static inline int phase_plan_structurally_valid(const phase_plan_t *plan)
{
    uint8_t i;
    if (plan == (const phase_plan_t *)0)          { return 0; }
    if (plan->num_phases == 0U)                   { return 0; }
    if (plan->num_phases > MAX_PHASES)            { return 0; }
    if (plan->approach_count == 0U)               { return 0; }
    if (plan->approach_count > MAX_APPROACHES)    { return 0; }
    for (i = 0U; i < plan->num_phases; i++)
    {
        if (plan->phases[i].min_duration_ms > plan->phases[i].max_duration_ms)
        {
            return 0;
        }
        if (plan->phases[i].max_duration_ms == 0U)
        {
            return 0;
        }
    }
    return 1;
}

/**
 * @brief Return the signal aspect for a given approach in a given phase.
 *
 * @param plan        Pointer to the phase plan.
 * @param phase_id    Phase index.
 * @param approach_id Approach index.
 * @return            signal_aspect_t, or ASPECT_RED if indices are out of range.
 */
static inline signal_aspect_t phase_plan_get_aspect(const phase_plan_t *plan,
                                                      uint8_t             phase_id,
                                                      uint8_t             approach_id)
{
    if (plan == (const phase_plan_t *)0)   { return ASPECT_RED; }
    if (phase_id    >= plan->num_phases)   { return ASPECT_RED; }
    if (approach_id >= MAX_APPROACHES)     { return ASPECT_RED; }
    return plan->phases[phase_id].aspect[approach_id];
}

#ifdef __cplusplus
}
#endif

#endif /* PHASE_PLAN_H */
