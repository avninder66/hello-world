/**
 * @file    conflict_matrix.c
 * @brief   Signal conflict matrix implementation for the Lumina safety supervisor.
 *
 * This module maintains the static conflict table and the runtime active-output
 * bitmask.  All logic runs in the STM32G071 10 ms safety superloop — no heap,
 * no RTOS, no blocking.
 *
 * Conflict rules (derived from UK TR 2500 / ITS standard for temporary signals):
 *
 *   Approach greens conflict with ALL other approach greens and with PED_GREEN.
 *   PED_GREEN conflicts with all vehicle greens.
 *   Amber aspects do NOT conflict with each other (simultaneous amber is safe
 *   during intergreen transitions).
 *   Red aspects do NOT conflict — multiple reds may coexist.
 *   AUX outputs have no conflicts with signal aspects by default.
 *
 * The "green-requires-prior-red" rule: a green aspect may only be granted if
 * the corresponding red for the same approach was active in the previous cycle.
 * This prevents a green appearing without a preceding red clearance phase.
 *
 * Copyright (c) Lumina Traffic Systems Ltd.  All rights reserved.
 * SPDX-License-Identifier: Proprietary
 */

#include "conflict_matrix.h"
#include <stddef.h>
#include <string.h>

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/**
 * Find the lowest set bit in a 32-bit mask and return it as an output_id_t.
 * Returns MAX_OUTPUTS if mask is zero.
 */
static output_id_t lowest_set_bit_id(uint32_t mask)
{
    if (mask == 0UL) { return (output_id_t)MAX_OUTPUTS; }
    uint32_t bit = 0U;
    while (((mask >> bit) & 1UL) == 0UL) { bit++; }
    return (output_id_t)bit;
}

/* =========================================================================
 * Static conflict table
 *
 * Each entry encodes: for output X, which other outputs conflict with it?
 * The table is fully symmetric — if A conflcts_with B, B conflicts_with A.
 *
 * Bitmask notation: OUTPUT_BIT(id) == (1UL << id)
 *
 *   Bit  0 = OUT_APPROACH_A_RED
 *   Bit  1 = OUT_APPROACH_A_AMBER
 *   Bit  2 = OUT_APPROACH_A_GREEN
 *   Bit  3 = OUT_APPROACH_B_RED
 *   Bit  4 = OUT_APPROACH_B_AMBER
 *   Bit  5 = OUT_APPROACH_B_GREEN
 *   Bit  6 = OUT_APPROACH_C_RED
 *   Bit  7 = OUT_APPROACH_C_AMBER
 *   Bit  8 = OUT_APPROACH_C_GREEN
 *   Bit  9 = OUT_APPROACH_D_RED
 *   Bit 10 = OUT_APPROACH_D_AMBER
 *   Bit 11 = OUT_APPROACH_D_GREEN
 *   Bit 12 = OUT_PED_RED
 *   Bit 13 = OUT_PED_GREEN
 *   Bit 14 = OUT_PED_WAIT
 *   Bit 15 = OUT_AUX_1
 *   Bit 16 = OUT_AUX_2
 * ========================================================================= */

/* Shorthand bitmasks for the vehicle green outputs and the pedestrian green. */
#define BIT_A_GREEN     OUTPUT_BIT(OUT_APPROACH_A_GREEN)
#define BIT_B_GREEN     OUTPUT_BIT(OUT_APPROACH_B_GREEN)
#define BIT_C_GREEN     OUTPUT_BIT(OUT_APPROACH_C_GREEN)
#define BIT_D_GREEN     OUTPUT_BIT(OUT_APPROACH_D_GREEN)
#define BIT_PED_GREEN   OUTPUT_BIT(OUT_PED_GREEN)

/* All vehicle greens combined — used to build the pedestrian green conflict row. */
#define ALL_VEH_GREENS  (BIT_A_GREEN | BIT_B_GREEN | BIT_C_GREEN | BIT_D_GREEN)

/* All other vehicle greens (used to build each approach green row). */
#define OTHER_VEH_GREENS_FROM_A  (BIT_B_GREEN | BIT_C_GREEN | BIT_D_GREEN | BIT_PED_GREEN)
#define OTHER_VEH_GREENS_FROM_B  (BIT_A_GREEN | BIT_C_GREEN | BIT_D_GREEN | BIT_PED_GREEN)
#define OTHER_VEH_GREENS_FROM_C  (BIT_A_GREEN | BIT_B_GREEN | BIT_D_GREEN | BIT_PED_GREEN)
#define OTHER_VEH_GREENS_FROM_D  (BIT_A_GREEN | BIT_B_GREEN | BIT_C_GREEN | BIT_PED_GREEN)

static const conflict_entry_t conflict_table[MAX_OUTPUTS] =
{
    /* ---- Approach A ---- */
    [OUT_APPROACH_A_RED]    = { OUT_APPROACH_A_RED,   0UL },
    [OUT_APPROACH_A_AMBER]  = { OUT_APPROACH_A_AMBER, 0UL },
    [OUT_APPROACH_A_GREEN]  = { OUT_APPROACH_A_GREEN, OTHER_VEH_GREENS_FROM_A },

    /* ---- Approach B ---- */
    [OUT_APPROACH_B_RED]    = { OUT_APPROACH_B_RED,   0UL },
    [OUT_APPROACH_B_AMBER]  = { OUT_APPROACH_B_AMBER, 0UL },
    [OUT_APPROACH_B_GREEN]  = { OUT_APPROACH_B_GREEN, OTHER_VEH_GREENS_FROM_B },

    /* ---- Approach C ---- */
    [OUT_APPROACH_C_RED]    = { OUT_APPROACH_C_RED,   0UL },
    [OUT_APPROACH_C_AMBER]  = { OUT_APPROACH_C_AMBER, 0UL },
    [OUT_APPROACH_C_GREEN]  = { OUT_APPROACH_C_GREEN, OTHER_VEH_GREENS_FROM_C },

    /* ---- Approach D ---- */
    [OUT_APPROACH_D_RED]    = { OUT_APPROACH_D_RED,   0UL },
    [OUT_APPROACH_D_AMBER]  = { OUT_APPROACH_D_AMBER, 0UL },
    [OUT_APPROACH_D_GREEN]  = { OUT_APPROACH_D_GREEN, OTHER_VEH_GREENS_FROM_D },

    /* ---- Pedestrian signal ---- */
    [OUT_PED_RED]           = { OUT_PED_RED,   0UL },
    /* PED_GREEN conflicts with every vehicle green. */
    [OUT_PED_GREEN]         = { OUT_PED_GREEN, ALL_VEH_GREENS },
    /* PED_WAIT (audible/tactile) does not conflict with vehicle aspects. */
    [OUT_PED_WAIT]          = { OUT_PED_WAIT,  0UL },

    /* ---- Auxiliary outputs — no signal conflicts by default ---- */
    [OUT_AUX_1]             = { OUT_AUX_1, 0UL },
    [OUT_AUX_2]             = { OUT_AUX_2, 0UL },

    /* ---- Reserved slots ---- */
    [OUT_RESERVED_17]       = { OUT_RESERVED_17, 0UL },
    [OUT_RESERVED_18]       = { OUT_RESERVED_18, 0UL },
    [OUT_RESERVED_19]       = { OUT_RESERVED_19, 0UL },
};

/* =========================================================================
 * Green-requires-prior-red mapping
 *
 * For each green output bit, which red bit must have been set in the previous
 * cycle?  Index is the output_id of the green; value is the OUTPUT_BIT of its
 * corresponding red.
 * ========================================================================= */

typedef struct
{
    output_id_t green_id;
    output_id_t required_red_id;
} green_red_pair_t;

static const green_red_pair_t green_red_pairs[] =
{
    { OUT_APPROACH_A_GREEN, OUT_APPROACH_A_RED },
    { OUT_APPROACH_B_GREEN, OUT_APPROACH_B_RED },
    { OUT_APPROACH_C_GREEN, OUT_APPROACH_C_RED },
    { OUT_APPROACH_D_GREEN, OUT_APPROACH_D_RED },
    { OUT_PED_GREEN,        OUT_PED_RED        },
};

#define NUM_GREEN_RED_PAIRS \
    (sizeof(green_red_pairs) / sizeof(green_red_pairs[0]))

/* =========================================================================
 * conflict_matrix_init
 * ========================================================================= */

bool conflict_matrix_init(conflict_matrix_state_t *state)
{
    if (state == NULL) { return false; }

    state->active_outputs    = 0UL;
    state->prev_cycle_active = 0UL;
    state->initialised       = false;

    /*
     * Symmetry check: for every entry E with conflicts_with bit j set,
     * conflict_table[j].conflicts_with must have OUTPUT_BIT(E.output_id) set.
     */
    for (uint32_t i = 0U; i < MAX_OUTPUTS; i++)
    {
        uint32_t cfmask = conflict_table[i].conflicts_with;
        uint32_t remaining = cfmask;
        while (remaining != 0UL)
        {
            uint32_t j = 0U;
            uint32_t tmp = remaining;
            while (((tmp >> j) & 1UL) == 0UL) { j++; }
            remaining &= ~(1UL << j);

            if (j >= MAX_OUTPUTS) { return false; }

            /* Check the reverse direction. */
            if ((conflict_table[j].conflicts_with & OUTPUT_BIT((output_id_t)i)) == 0UL)
            {
                /* Table is asymmetric — configuration error. */
                return false;
            }
        }
    }

    state->initialised = true;
    return true;
}

/* =========================================================================
 * conflict_matrix_check_permissive
 * ========================================================================= */

permissive_result_t conflict_matrix_check_permissive(
        conflict_matrix_state_t    *state,
        const permissive_request_t *request)
{
    permissive_result_t result;
    result.granted            = false;
    result.denial_reason      = DENIAL_NONE;
    result.conflicting_output = (output_id_t)MAX_OUTPUTS;

    if ((state == NULL) || !state->initialised || (request == NULL))
    {
        result.denial_reason = SUPERVISOR_FAULT_ACTIVE;
        return result;
    }

    uint32_t req_mask   = request->outputs_to_enable;
    uint32_t active     = state->active_outputs;
    uint32_t prev_cycle = state->prev_cycle_active;

    /* Iterate over each requested output bit. */
    uint32_t remaining = req_mask;
    while (remaining != 0UL)
    {
        /* Extract lowest set bit. */
        uint32_t bit_pos = 0U;
        uint32_t tmp = remaining;
        while (((tmp >> bit_pos) & 1UL) == 0UL) { bit_pos++; }
        remaining &= ~(1UL << bit_pos);

        if (bit_pos >= MAX_OUTPUTS)
        {
            result.denial_reason      = INVALID_OUTPUT_ID;
            result.conflicting_output = (output_id_t)bit_pos;
            return result;
        }

        output_id_t out_id = (output_id_t)bit_pos;

        /* --- Conflict matrix check --- */
        uint32_t conflict_mask = conflict_table[out_id].conflicts_with;
        uint32_t collision     = active & conflict_mask;

        if (collision != 0UL)
        {
            result.denial_reason      = CONFLICT_MATRIX_FAIL;
            result.conflicting_output = lowest_set_bit_id(collision);
            return result;
        }

        /*
         * --- Green-requires-prior-red check ---
         *
         * Search the green_red_pairs table for this output ID.
         * If found, verify the corresponding red was active last cycle.
         */
        for (uint32_t p = 0U; p < NUM_GREEN_RED_PAIRS; p++)
        {
            if (green_red_pairs[p].green_id == out_id)
            {
                uint32_t required_red_bit = OUTPUT_BIT(green_red_pairs[p].required_red_id);
                if ((prev_cycle & required_red_bit) == 0UL)
                {
                    result.denial_reason      = NO_PRIOR_RED;
                    result.conflicting_output = out_id;
                    return result;
                }
                break;
            }
        }
    }

    result.granted       = true;
    result.denial_reason = DENIAL_NONE;
    return result;
}

/* =========================================================================
 * conflict_matrix_update_active_outputs
 * ========================================================================= */

void conflict_matrix_update_active_outputs(conflict_matrix_state_t *state,
                                           uint32_t                 new_active_mask)
{
    if (state == NULL) { return; }

    /*
     * Roll the current active mask into prev_cycle before overwriting.
     * This is called once per 10 ms cycle so prev_cycle always reflects the
     * state from the previous execution of the superloop.
     */
    state->prev_cycle_active = state->active_outputs;
    state->active_outputs    = new_active_mask & ((1UL << MAX_OUTPUTS) - 1UL);
}

/* =========================================================================
 * conflict_matrix_get_active_outputs
 * ========================================================================= */

uint32_t conflict_matrix_get_active_outputs(const conflict_matrix_state_t *state)
{
    if (state == NULL) { return 0UL; }
    return state->active_outputs;
}

/* =========================================================================
 * conflict_matrix_get_entry
 * ========================================================================= */

const conflict_entry_t *conflict_matrix_get_entry(output_id_t id)
{
    if ((uint32_t)id >= MAX_OUTPUTS) { return NULL; }
    return &conflict_table[(uint32_t)id];
}
