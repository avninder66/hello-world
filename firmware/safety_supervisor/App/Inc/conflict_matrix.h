/**
 * @file    conflict_matrix.h
 * @brief   Signal conflict matrix and permissive logic for the Lumina safety supervisor.
 *
 * The conflict matrix is a static lookup table that encodes which signal output
 * aspects are mutually exclusive (i.e. must never be energised simultaneously).
 * All conflict checks run on the STM32G071 safety supervisor MCU in the
 * deterministic 10 ms superloop — no dynamic memory, no RTOS.
 *
 * Safety note: this module is part of the safety-critical software partition.
 * All functions must complete within the 10 ms cycle budget.  No blocking calls
 * or dynamic allocation are permitted.
 *
 * Copyright (c) Lumina Traffic Systems Ltd.  All rights reserved.
 * SPDX-License-Identifier: Proprietary
 */

#ifndef CONFLICT_MATRIX_H
#define CONFLICT_MATRIX_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Output channel definitions
 * ========================================================================= */

/** Maximum number of independently controlled signal outputs. */
#define MAX_OUTPUTS     20U

/**
 * @brief Logical output identifiers.
 *
 * Each enumerator maps to a single lamp driver channel on the LSO-100 output
 * board.  The numeric value is also the bit position used in the 32-bit bitmask
 * fields throughout this module.
 */
typedef enum
{
    OUT_APPROACH_A_RED      = 0U,   /**< Approach A — red aspect                    */
    OUT_APPROACH_A_AMBER    = 1U,   /**< Approach A — amber aspect                  */
    OUT_APPROACH_A_GREEN    = 2U,   /**< Approach A — green aspect                  */

    OUT_APPROACH_B_RED      = 3U,   /**< Approach B — red aspect                    */
    OUT_APPROACH_B_AMBER    = 4U,   /**< Approach B — amber aspect                  */
    OUT_APPROACH_B_GREEN    = 5U,   /**< Approach B — green aspect                  */

    OUT_APPROACH_C_RED      = 6U,   /**< Approach C — red aspect                    */
    OUT_APPROACH_C_AMBER    = 7U,   /**< Approach C — amber aspect                  */
    OUT_APPROACH_C_GREEN    = 8U,   /**< Approach C — green aspect                  */

    OUT_APPROACH_D_RED      = 9U,   /**< Approach D — red aspect                    */
    OUT_APPROACH_D_AMBER    = 10U,  /**< Approach D — amber aspect                  */
    OUT_APPROACH_D_GREEN    = 11U,  /**< Approach D — green aspect                  */

    OUT_PED_RED             = 12U,  /**< Pedestrian signal — red (don't walk)       */
    OUT_PED_GREEN           = 13U,  /**< Pedestrian signal — green (walk)           */
    OUT_PED_WAIT            = 14U,  /**< Pedestrian signal — wait indicator         */

    OUT_AUX_1               = 15U,  /**< Auxiliary output 1 (site-specific)         */
    OUT_AUX_2               = 16U,  /**< Auxiliary output 2 (site-specific)         */

    /* IDs 17-19 reserved for future expansion — do not use */
    OUT_RESERVED_17         = 17U,
    OUT_RESERVED_18         = 18U,
    OUT_RESERVED_19         = 19U,
} output_id_t;

/** Convenience macro: convert an output_id_t to its single-bit bitmask. */
#define OUTPUT_BIT(id)      (1UL << (uint32_t)(id))

/* =========================================================================
 * Conflict table entry
 * ========================================================================= */

/**
 * @brief One row in the static conflict matrix.
 *
 * For output @p output_id, the @p conflicts_with bitmask has a 1 at bit
 * position j whenever output_id and j must not be simultaneously active.
 * The table is symmetric: if A conflicts with B then B conflicts with A.
 */
typedef struct
{
    output_id_t output_id;          /**< The output this row describes              */
    uint32_t    conflicts_with;     /**< Bitmask of outputs that conflict with it   */
} conflict_entry_t;

/* =========================================================================
 * Permissive request / result types
 * ========================================================================= */

/**
 * @brief Permissive request submitted by the main MCU via SPI.
 *
 * The supervisor evaluates this request against the static conflict table and
 * the current active-output state before granting or denying the transition.
 */
typedef struct
{
    uint8_t  requested_phase_id;    /**< Phase ID being requested (0-based)         */
    uint32_t outputs_to_enable;     /**< Bitmask of OUTPUT_BIT(output_id_t) values  */
    uint32_t calling_tick;          /**< Value of 10 ms system tick at time of call */
} permissive_request_t;

/**
 * @brief Denial reason codes returned in permissive_result_t.
 *
 * These map directly to lumina_denial_reason_t in the SPI protocol but are
 * defined here to avoid a dependency on the protocol header in application code.
 */
typedef enum
{
    DENIAL_NONE                 = 0x00U,    /**< No denial (result is granted)       */
    CONFLICT_MATRIX_FAIL        = 0x01U,    /**< Conflict with an active output      */
    INTERGREEN_NOT_ELAPSED      = 0x02U,    /**< Intergreen timer still running      */
    NO_PRIOR_RED                = 0x03U,    /**< Green requested without prior red   */
    INVALID_OUTPUT_ID           = 0x04U,    /**< Output ID is out of range           */
    SUPERVISOR_FAULT_ACTIVE     = 0x05U,    /**< Latched fault prevents grant        */
} denial_reason_t;

/**
 * @brief Result returned from conflict_matrix_check_permissive().
 */
typedef struct
{
    bool            granted;            /**< true = permissive granted              */
    denial_reason_t denial_reason;      /**< Populated when granted == false        */
    output_id_t     conflicting_output; /**< First conflicting output when denied   */
} permissive_result_t;

/* =========================================================================
 * Module state
 * ========================================================================= */

/**
 * @brief Opaque module state accessed only through the API functions below.
 *
 * Declared here so callers can embed it without heap allocation; initialised
 * by conflict_matrix_init() and subsequently owned by the module.
 */
typedef struct
{
    uint32_t active_outputs;           /**< Bitmask of currently energised outputs  */
    uint32_t prev_cycle_active;        /**< Active outputs in the previous 10 ms cycle */
    bool     initialised;              /**< Set to true after conflict_matrix_init() */
} conflict_matrix_state_t;

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * @brief Initialise the conflict matrix module.
 *
 * Must be called once during startup before any other conflict_matrix_*
 * function.  Clears all active outputs and verifies the compile-time conflict
 * table for internal consistency (symmetric check).
 *
 * @param state  Pointer to caller-allocated module state structure.
 * @return       true if initialisation succeeded and table passed sanity check.
 */
bool conflict_matrix_init(conflict_matrix_state_t *state);

/**
 * @brief Check whether a permissive request can be granted.
 *
 * Evaluates the requested output bitmask against:
 *   1. The static conflict table (no conflicting outputs may be active).
 *   2. The green-requires-prior-red rule (each green bit must have had the
 *      corresponding red bit set in prev_cycle_active).
 *
 * Does NOT modify module state — call conflict_matrix_update_active_outputs()
 * after the phase transition has been committed to hardware.
 *
 * @param state    Pointer to module state (must have been initialised).
 * @param request  Permissive request to evaluate.
 * @return         permissive_result_t with granted/denied outcome.
 */
permissive_result_t conflict_matrix_check_permissive(
        conflict_matrix_state_t       *state,
        const permissive_request_t    *request);

/**
 * @brief Update the active output bitmask after a committed phase transition.
 *
 * Must be called once per 10 ms cycle to keep prev_cycle_active current, and
 * immediately after a phase is committed so active_outputs reflects reality.
 *
 * @param state           Pointer to module state.
 * @param new_active_mask Bitmask of outputs that are now energised.
 */
void conflict_matrix_update_active_outputs(conflict_matrix_state_t *state,
                                           uint32_t                 new_active_mask);

/**
 * @brief Return the current active output bitmask (for diagnostic use).
 *
 * @param state  Pointer to module state.
 * @return       Bitmask of currently active outputs.
 */
uint32_t conflict_matrix_get_active_outputs(const conflict_matrix_state_t *state);

/**
 * @brief Return the static conflict entry for a given output ID.
 *
 * Useful for diagnostics and for building the SPI conflict-matrix frame.
 *
 * @param id  Output identifier.
 * @return    Pointer to the static conflict_entry_t, or NULL if id is invalid.
 */
const conflict_entry_t *conflict_matrix_get_entry(output_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* CONFLICT_MATRIX_H */
