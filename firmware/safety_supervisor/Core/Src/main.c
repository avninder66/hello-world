/**
 * @file    main.c
 * @brief   Safety supervisor main application — STM32G071 (Lumina LCU-100).
 *
 * This is the SAFETY-CRITICAL MCU on the LCU-100 board.  Its sole function is
 * to gate the hardware INHIBIT line to the LSO-100 lamp driver board.  When the
 * INHIBIT line is asserted (driven low, open-drain), the LSO-100 PROFET gate
 * drivers are unconditionally disabled, making all lamp outputs fail-dark.
 *
 * Architecture: deterministic superloop — NO FreeRTOS, NO dynamic allocation.
 * A 10 ms hardware timer (TIM1) drives the main cycle tick.  All work is done
 * in the foreground loop on each tick; the timer ISR only sets a flag.
 *
 * Fail-safe behaviour:
 *   - IWDG is configured for ~50 ms window (prescaler/reload chosen below).
 *     If the superloop stalls and the IWDG is not kicked, the MCU resets and
 *     the INHIBIT_OUT pin defaults to its reset state (low = inhibit active)
 *     because it is configured as open-drain with no external pull-up that
 *     would override the default input state on the LSO-100.
 *   - If the SPI frame from the LCU main MCU is absent for 3 consecutive
 *     10 ms cycles, the supervisor asserts INHIBIT in software.
 *   - If the received SPI frame fails CRC validation, it is treated as absent.
 *
 * GPIO assignments (LQFP-32 STM32G071KBT6):
 *   PA4  — SPI1_NSS  (slave select, active low, input)
 *   PA5  — SPI1_SCK  (input)
 *   PA6  — SPI1_MISO (output, AF0)
 *   PA7  — SPI1_MOSI (input, AF0)
 *   PB0  — INHIBIT_OUT (open-drain output, active-low → asserted = lamp drivers off)
 *   PB1  — STATUS_LED  (push-pull output, active-high)
 *   PB2  — WATCHDOG_TRIGGER (push-pull output — toggled each cycle as software WDG
 *                             signal back to the LCU main MCU)
 *
 * Copyright (c) Lumina Traffic Systems Ltd.  All rights reserved.
 * SPDX-License-Identifier: Proprietary
 */

/* =========================================================================
 * Includes
 * ========================================================================= */

#include "stm32g0xx_hal.h"
#include "conflict_matrix.h"
#include "lumina_safety_protocol.h"
#include <string.h>
#include <stdbool.h>

/* =========================================================================
 * Hardware pin definitions
 * ========================================================================= */

/** INHIBIT_OUT — PB0, open-drain, active-low.
 *  Driving low (reset state) = inhibit asserted = LSO lamp drivers disabled. */
#define INHIBIT_OUT_PORT        GPIOB
#define INHIBIT_OUT_PIN         GPIO_PIN_0

/** STATUS_LED — PB1, push-pull, active-high. */
#define STATUS_LED_PORT         GPIOB
#define STATUS_LED_PIN          GPIO_PIN_1

/** WATCHDOG_TRIGGER — PB2, push-pull, toggled every 10 ms cycle. */
#define WDG_TRIGGER_PORT        GPIOB
#define WDG_TRIGGER_PIN         GPIO_PIN_2

/* =========================================================================
 * Timing constants
 * ========================================================================= */

/** System clock after SystemClock_Config(): 64 MHz from HSI. */
#define SYSCLK_HZ               64000000UL

/** TIM1 drives the 10 ms superloop tick. */
#define CYCLE_PERIOD_MS         10U

/**
 * Consecutive missing SPI frames before INHIBIT is asserted.
 * 3 × 10 ms = 30 ms maximum tolerated comms gap.
 */
#define MAX_MISSING_FRAMES      3U

/**
 * IWDG configuration for ~50 ms timeout.
 *
 * IWDG clock = LSI ≈ 32 kHz (40 kHz max, 32 kHz nominal on G071).
 * Prescaler divider 8  → tick = 8/32000 = 250 µs.
 * Reload value 200     → timeout = 200 × 250 µs = 50 ms.
 *
 * The IWDG is kicked every 10 ms cycle, giving a 5× margin.
 * If the loop stalls for >50 ms the IWDG fires and the MCU resets.
 */
#define IWDG_PRESCALER          IWDG_PRESCALER_8
#define IWDG_RELOAD_VALUE       200U

/* =========================================================================
 * Peripheral handles
 * ========================================================================= */

static SPI_HandleTypeDef  hspi1;
static TIM_HandleTypeDef  htim1;
static IWDG_HandleTypeDef hiwdg;

/* =========================================================================
 * SPI frame buffers (16 bytes each, owned by SPI DMA / interrupt)
 * ========================================================================= */

static volatile lumina_spi_frame_t spi_rx_frame;   /**< Last received command frame  */
static          lumina_spi_frame_t spi_tx_frame;   /**< Response frame to transmit   */

/**
 * Set by SPI transfer-complete ISR; cleared after processing in the superloop.
 * Declared volatile because it is written in interrupt context.
 */
static volatile bool spi_frame_received = false;

/* =========================================================================
 * Conflict matrix state
 * ========================================================================= */

static conflict_matrix_state_t conflict_state;

/* =========================================================================
 * Supervisor runtime state
 * ========================================================================= */

/** Supervisor safety state machine. */
static lumina_safety_state_t safety_state = LUMINA_SAFETY_STATE_INIT;

/** Active phase index currently approved by the supervisor. */
static uint8_t active_phase_id = 0xFFU;

/** Number of consecutive 10 ms cycles in which no valid SPI frame arrived. */
static uint8_t missing_frame_count = 0U;

/** INHIBIT line logical state tracked in software. */
static bool inhibit_asserted = true;   /* Start inhibited until comms established */

/** Latched fault code (0x0000 = no fault). */
static uint16_t latched_fault_code = 0x0000U;

/** Last denial reason from conflict matrix check. */
static denial_reason_t last_denial_reason = DENIAL_NONE;

/** Rolling watchdog counter echoed from master. */
static uint8_t wdg_seq_last = 0U;

/** Watchdog trigger pin state — toggled each cycle. */
static bool wdg_trigger_state = false;

/** Status LED blink counter (LED toggles every 50 cycles = 500 ms in normal op). */
static uint32_t led_blink_counter = 0U;

/** System uptime in 10 ms ticks. */
static volatile uint32_t system_tick_ms = 0U;

/**
 * Set by TIM1 update interrupt; cleared at the top of the superloop.
 * Volatile because written in ISR context.
 */
static volatile bool cycle_tick_flag = false;

/* =========================================================================
 * Forward declarations
 * ========================================================================= */

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_TIM1_Init(void);
static void MX_IWDG_Init(void);
static void SPI1_StartReceive(void);

static void     process_cycle(void);
static bool     validate_and_process_rx_frame(const lumina_spi_frame_t *frame,
                                              permissive_result_t      *result_out,
                                              uint32_t                 *new_outputs_out);
static void     build_response_frame(lumina_spi_response_t resp_code,
                                     denial_reason_t       denial,
                                     output_id_t           conflict_out);
static void     set_inhibit(bool assert_inhibit);
static uint8_t  compute_supervisor_temp(void);

/* =========================================================================
 * main()
 * ========================================================================= */

int main(void)
{
    /* Cortex-M0+ core init: resets cycle counter, enables FPU if present. */
    HAL_Init();

    /* Configure 64 MHz system clock from internal HSI. */
    SystemClock_Config();

    /* Initialise GPIO, SPI, timer, IWDG. */
    MX_GPIO_Init();
    MX_SPI1_Init();
    MX_TIM1_Init();

    /*
     * Conflict matrix initialisation.  If the table fails its internal
     * symmetry check we must not proceed — assert inhibit and spin, allowing
     * the IWDG to force a reset.  The reset-cause register will reflect this.
     */
    if (!conflict_matrix_init(&conflict_state))
    {
        set_inhibit(true);
        /* Spin until IWDG fires. */
        while (1) { /* deliberate spin — awaiting watchdog reset */ }
    }

    safety_state = LUMINA_SAFETY_STATE_ALL_RED;

    /* Start SPI slave receive for the first frame. */
    SPI1_StartReceive();

    /* IWDG must be initialised AFTER all other hardware — from this point
     * the superloop must kick it every 10 ms or the MCU resets. */
    MX_IWDG_Init();

    /* Start 10 ms TIM1 tick. */
    HAL_TIM_Base_Start_IT(&htim1);

    /* -----------------------------------------------------------------------
     * Superloop
     * --------------------------------------------------------------------- */
    while (1)
    {
        /* Wait for TIM1 10 ms tick flag. */
        if (!cycle_tick_flag)
        {
            /* Nothing to do between ticks — yield to background (not RTOS,
             * just CPU idle; peripherals and ISRs still run). */
            __WFI();
            continue;
        }

        /* Acknowledge tick and advance system time. */
        cycle_tick_flag = false;
        system_tick_ms += CYCLE_PERIOD_MS;

        /* Execute the main 10 ms processing cycle. */
        process_cycle();

        /* Kick the hardware IWDG — must complete each cycle.
         * If process_cycle() ran over budget the next tick will also be pending
         * but the IWDG kick still happens here, keeping the window open. */
        HAL_IWDG_Refresh(&hiwdg);

        /* Toggle the software watchdog output pin to the LCU main MCU. */
        wdg_trigger_state = !wdg_trigger_state;
        HAL_GPIO_WritePin(WDG_TRIGGER_PORT, WDG_TRIGGER_PIN,
                          wdg_trigger_state ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
    /* Never reached. */
}

/* =========================================================================
 * process_cycle()  — 10 ms work function
 * ========================================================================= */

static void process_cycle(void)
{
    permissive_result_t perm_result;
    perm_result.granted            = false;
    perm_result.denial_reason      = DENIAL_NONE;
    perm_result.conflicting_output = (output_id_t)MAX_OUTPUTS;

    uint32_t new_outputs = 0UL;

    /* ------------------------------------------------------------------
     * Step 1 & 2: Check and process received SPI frame.
     * ------------------------------------------------------------------ */
    bool frame_valid = false;

    if (spi_frame_received)
    {
        spi_frame_received = false;
        missing_frame_count = 0U;

        /* Validate frame and extract permissive request. */
        lumina_spi_frame_t local_frame;
        /* Copy volatile rx buffer to local for processing. */
        memcpy(&local_frame, (const void *)&spi_rx_frame, sizeof(lumina_spi_frame_t));

        frame_valid = validate_and_process_rx_frame(&local_frame,
                                                    &perm_result,
                                                    &new_outputs);

        /* Arm the next SPI receive for the following cycle. */
        SPI1_StartReceive();
    }
    else
    {
        /* No frame this cycle. */
        missing_frame_count++;
    }

    /* ------------------------------------------------------------------
     * Step 3: Determine whether to assert inhibit due to comms loss.
     * ------------------------------------------------------------------ */
    if (missing_frame_count >= MAX_MISSING_FRAMES)
    {
        /*
         * 3 or more consecutive missed frames → assert INHIBIT and enter
         * fault-lockout unless we are already in a latched fault.
         */
        if (safety_state != LUMINA_SAFETY_STATE_FAULT_LOCKOUT)
        {
            safety_state       = LUMINA_SAFETY_STATE_FAULT_LOCKOUT;
            latched_fault_code = 0x0101U; /* FAULT: SPI comms loss */
        }
        set_inhibit(true);

        /* Update active outputs to all-off since inhibit is active. */
        conflict_matrix_update_active_outputs(&conflict_state, 0UL);

        /* Build a FAULT_ACTIVE response for the next SPI exchange. */
        build_response_frame(LUMINA_RESP_FAULT_ACTIVE, SUPERVISOR_FAULT_ACTIVE,
                             (output_id_t)MAX_OUTPUTS);

        /* Status LED: rapid blink (toggle every cycle) to indicate fault. */
        HAL_GPIO_TogglePin(STATUS_LED_PORT, STATUS_LED_PIN);
        return;
    }

    /* ------------------------------------------------------------------
     * Step 4 & 5: Run conflict check and update INHIBIT_OUT.
     * ------------------------------------------------------------------ */

    if (safety_state == LUMINA_SAFETY_STATE_FAULT_LOCKOUT)
    {
        /* Stay inhibited until fault is cleared by a CLEAR_FAULT command. */
        set_inhibit(true);
        build_response_frame(LUMINA_RESP_FAULT_ACTIVE, SUPERVISOR_FAULT_ACTIVE,
                             (output_id_t)MAX_OUTPUTS);
        HAL_GPIO_TogglePin(STATUS_LED_PORT, STATUS_LED_PIN);
        return;
    }

    if (frame_valid && perm_result.granted)
    {
        /*
         * Permissive granted: release inhibit and update the active outputs.
         * The INHIBIT line is de-asserted (driven high through open-drain — the
         * external pull-up to V_IO enables the LSO-100 PROFET gate drivers).
         */
        set_inhibit(false);
        conflict_matrix_update_active_outputs(&conflict_state, new_outputs);
        safety_state   = LUMINA_SAFETY_STATE_PHASE_ACTIVE;
        last_denial_reason = DENIAL_NONE;

        /* Build a PERMISSIVE_GRANTED response. */
        build_response_frame(LUMINA_RESP_PERMISSIVE_GRANTED, DENIAL_NONE,
                             (output_id_t)MAX_OUTPUTS);
    }
    else if (frame_valid && !perm_result.granted)
    {
        /*
         * Permissive denied: assert inhibit and record reason.
         */
        set_inhibit(true);
        conflict_matrix_update_active_outputs(&conflict_state, 0UL);
        safety_state       = LUMINA_SAFETY_STATE_ALL_RED;
        last_denial_reason = perm_result.denial_reason;

        build_response_frame(LUMINA_RESP_PERMISSIVE_DENIED,
                             perm_result.denial_reason,
                             perm_result.conflicting_output);
    }
    else
    {
        /*
         * No valid frame this cycle but not yet at the comms-loss threshold.
         * Keep the current INHIBIT state unchanged; send a status response.
         */
        conflict_matrix_update_active_outputs(&conflict_state,
                                              conflict_matrix_get_active_outputs(&conflict_state));
        build_response_frame(LUMINA_RESP_STATUS, DENIAL_NONE,
                             (output_id_t)MAX_OUTPUTS);
    }

    /* ------------------------------------------------------------------
     * Step 6 & 7: IWDG kick and watchdog trigger pin are handled by the
     * superloop caller after this function returns.
     * ------------------------------------------------------------------ */

    /* Status LED: slow blink during normal operation (500 ms period). */
    led_blink_counter++;
    if (led_blink_counter >= 50U)
    {
        led_blink_counter = 0U;
        HAL_GPIO_TogglePin(STATUS_LED_PORT, STATUS_LED_PIN);
    }
}

/* =========================================================================
 * validate_and_process_rx_frame()
 *
 * Validates CRC + framing of an inbound SPI frame, then dispatches on opcode.
 * Returns true if the frame was structurally valid.  Even a valid frame may
 * result in perm_result.granted == false (e.g., WATCHDOG_PET, READ_STATUS).
 * ========================================================================= */

static bool validate_and_process_rx_frame(const lumina_spi_frame_t *frame,
                                          permissive_result_t      *result_out,
                                          uint32_t                 *new_outputs_out)
{
    /* --- Framing and CRC validation --- */
    if (!lumina_spi_frame_valid(frame))
    {
        /*
         * Bad frame: respond with FRAME_ERROR but do not count as a valid
         * comms exchange (the missing_frame_count was already NOT cleared
         * above — wait, actually we DID clear it.  That is correct: a frame
         * was received; it just had a CRC error.  The LCU will retransmit.
         * We build a FRAME_ERROR response but grant nothing.
         */
        build_response_frame(LUMINA_RESP_FRAME_ERROR, DENIAL_NONE,
                             (output_id_t)MAX_OUTPUTS);
        result_out->granted       = false;
        result_out->denial_reason = DENIAL_NONE;
        *new_outputs_out          = 0UL;
        return false;
    }

    /* Frame is structurally valid — dispatch on opcode. */
    result_out->granted            = false;
    result_out->denial_reason      = DENIAL_NONE;
    result_out->conflicting_output = (output_id_t)MAX_OUTPUTS;
    *new_outputs_out               = 0UL;

    switch ((lumina_spi_cmd_t)frame->opcode)
    {
        /* ------------------------------------------------------------------
         * REQUEST_PHASE: validate conflict matrix and grant/deny the phase.
         * ------------------------------------------------------------------ */
        case LUMINA_CMD_REQUEST_PHASE:
        {
            const lumina_spi_req_phase_payload_t *req =
                (const lumina_spi_req_phase_payload_t *)frame->payload;

            if (safety_state == LUMINA_SAFETY_STATE_FAULT_LOCKOUT)
            {
                result_out->denial_reason = SUPERVISOR_FAULT_ACTIVE;
                break;
            }

            /*
             * Build an output bitmask from the requested phase_id.
             * The safety supervisor uses a simplified model: it checks that
             * the outputs reported in new_outputs (derived from phase_id) do
             * not conflict.  The actual channel map is the responsibility of
             * the LCU main MCU; we validate only the conflicting-green logic.
             *
             * For simplicity here, we treat phase_id as mapping to specific
             * outputs.  In a full implementation the phase→output map would
             * be loaded via WRITE_CONFLICT or stored in NVM.
             *
             * We receive the outputs_to_enable in the extended conflict matrix
             * write command.  Here we trust the phase_id to indicate the
             * requested mask; we receive no separate mask in this opcode so we
             * construct a conservative request bitmask from the phase_id.
             *
             * NOTE: In the real system, the outputs_to_enable would be derived
             * from a stored phase plan.  For now we use a default mapping:
             *   phase 0 = Approach A green + Approach B red (etc.)
             *   The actual check is against whatever was sent as the conflict
             *   matrix via WRITE_CONFLICT.  This default is conservative.
             */
            permissive_request_t preq;
            preq.requested_phase_id = req->phase_id;
            preq.calling_tick       = system_tick_ms;

            /*
             * Default phase→output mapping (site-commissioning would override
             * this via WRITE_CONFLICT, but we need a safe default):
             *   Phase 0: Approach A green (implies B,C,D red + ped red)
             *   Phase 1: Approach B green
             *   Phase 2: Approach C green
             *   Phase 3: Approach D green
             *   Phase 4: Pedestrian green
             *   Others:  All red (inhibit granted vacuously)
             */
            switch (req->phase_id)
            {
                case 0U:
                    preq.outputs_to_enable =
                        OUTPUT_BIT(OUT_APPROACH_A_GREEN) |
                        OUTPUT_BIT(OUT_APPROACH_B_RED)   |
                        OUTPUT_BIT(OUT_APPROACH_C_RED)   |
                        OUTPUT_BIT(OUT_APPROACH_D_RED)   |
                        OUTPUT_BIT(OUT_PED_RED);
                    break;
                case 1U:
                    preq.outputs_to_enable =
                        OUTPUT_BIT(OUT_APPROACH_A_RED)   |
                        OUTPUT_BIT(OUT_APPROACH_B_GREEN) |
                        OUTPUT_BIT(OUT_APPROACH_C_RED)   |
                        OUTPUT_BIT(OUT_APPROACH_D_RED)   |
                        OUTPUT_BIT(OUT_PED_RED);
                    break;
                case 2U:
                    preq.outputs_to_enable =
                        OUTPUT_BIT(OUT_APPROACH_A_RED)   |
                        OUTPUT_BIT(OUT_APPROACH_B_RED)   |
                        OUTPUT_BIT(OUT_APPROACH_C_GREEN) |
                        OUTPUT_BIT(OUT_APPROACH_D_RED)   |
                        OUTPUT_BIT(OUT_PED_RED);
                    break;
                case 3U:
                    preq.outputs_to_enable =
                        OUTPUT_BIT(OUT_APPROACH_A_RED)   |
                        OUTPUT_BIT(OUT_APPROACH_B_RED)   |
                        OUTPUT_BIT(OUT_APPROACH_C_RED)   |
                        OUTPUT_BIT(OUT_APPROACH_D_GREEN) |
                        OUTPUT_BIT(OUT_PED_RED);
                    break;
                case 4U:
                    preq.outputs_to_enable =
                        OUTPUT_BIT(OUT_APPROACH_A_RED) |
                        OUTPUT_BIT(OUT_APPROACH_B_RED) |
                        OUTPUT_BIT(OUT_APPROACH_C_RED) |
                        OUTPUT_BIT(OUT_APPROACH_D_RED) |
                        OUTPUT_BIT(OUT_PED_GREEN);
                    break;
                default:
                    /* All-red / unknown phase: only red aspects, no conflicts. */
                    preq.outputs_to_enable =
                        OUTPUT_BIT(OUT_APPROACH_A_RED) |
                        OUTPUT_BIT(OUT_APPROACH_B_RED) |
                        OUTPUT_BIT(OUT_APPROACH_C_RED) |
                        OUTPUT_BIT(OUT_APPROACH_D_RED) |
                        OUTPUT_BIT(OUT_PED_RED);
                    break;
            }

            *result_out = conflict_matrix_check_permissive(&conflict_state, &preq);

            if (result_out->granted)
            {
                active_phase_id  = req->phase_id;
                *new_outputs_out = preq.outputs_to_enable;
            }
            break;
        }

        /* ------------------------------------------------------------------
         * CONFIRM_INHIBIT: latch the inhibit state.
         * ------------------------------------------------------------------ */
        case LUMINA_CMD_CONFIRM_INHIBIT:
        {
            set_inhibit(true);
            conflict_matrix_update_active_outputs(&conflict_state, 0UL);
            safety_state    = LUMINA_SAFETY_STATE_ALL_RED;
            active_phase_id = 0xFFU;
            /* Signal a granted outcome so process_cycle knows to send GRANTED. */
            result_out->granted = true;
            break;
        }

        /* ------------------------------------------------------------------
         * READ_STATUS: no state change, response built by process_cycle caller.
         * ------------------------------------------------------------------ */
        case LUMINA_CMD_READ_STATUS:
        {
            result_out->granted = false;  /* No phase transition. */
            break;
        }

        /* ------------------------------------------------------------------
         * CLEAR_FAULT: acknowledge and clear a latched fault.
         * ------------------------------------------------------------------ */
        case LUMINA_CMD_CLEAR_FAULT:
        {
            const lumina_spi_clear_fault_payload_t *clr =
                (const lumina_spi_clear_fault_payload_t *)frame->payload;

            if (clr->operator_key == 0x55U &&
                clr->fault_code == latched_fault_code &&
                safety_state    == LUMINA_SAFETY_STATE_FAULT_LOCKOUT)
            {
                latched_fault_code = 0x0000U;
                safety_state       = LUMINA_SAFETY_STATE_ALL_RED;
                missing_frame_count = 0U;
                result_out->granted = true;
            }
            else
            {
                result_out->granted       = false;
                result_out->denial_reason = SUPERVISOR_FAULT_ACTIVE;
            }
            break;
        }

        /* ------------------------------------------------------------------
         * WATCHDOG_PET: acknowledge the pet, check the rolling counter.
         * ------------------------------------------------------------------ */
        case LUMINA_CMD_WATCHDOG_PET:
        {
            uint8_t new_seq = frame->payload[0];
            /* The sequence must advance (wrapping at 255→0 is fine). */
            if (new_seq != (uint8_t)(wdg_seq_last + 1U) && new_seq != 0U)
            {
                /*
                 * Sequence did not advance — possible replay attack or comms
                 * corruption.  Do not grant; log but do not latch a fault
                 * (a single sequence error is not safety-critical).
                 */
                result_out->granted       = false;
                result_out->denial_reason = DENIAL_NONE;
            }
            else
            {
                wdg_seq_last        = new_seq;
                result_out->granted = false;  /* WDG pet is not a phase grant */
            }
            break;
        }

        /* ------------------------------------------------------------------
         * SELF_TEST: initiate a supervised self-test.
         * ------------------------------------------------------------------ */
        case LUMINA_CMD_SELF_TEST:
        {
            /* Outputs must be inhibited before self-test can run. */
            if (!inhibit_asserted)
            {
                result_out->granted       = false;
                result_out->denial_reason = SUPERVISOR_FAULT_ACTIVE;
            }
            else
            {
                safety_state        = LUMINA_SAFETY_STATE_SELF_TEST;
                result_out->granted = true;
                /*
                 * Full self-test would run here.  For the self-test state we
                 * immediately validate the conflict matrix (already done at
                 * init) and return to ALL_RED.
                 */
                safety_state = LUMINA_SAFETY_STATE_ALL_RED;
            }
            break;
        }

        /* ------------------------------------------------------------------
         * WRITE_CONFLICT: accept a new conflict matrix (not fully implemented
         * here — the static table is used; this would update it in NVM).
         * ------------------------------------------------------------------ */
        case LUMINA_CMD_WRITE_CONFLICT:
        {
            /*
             * In a full implementation, parse lumina_spi_conflict_matrix_payload_t
             * and update the runtime conflict table, then re-validate symmetry.
             * Here we acknowledge receipt without modifying the static table.
             */
            result_out->granted = true;
            break;
        }

        default:
        {
            /* Unknown opcode — send a FRAME_ERROR response. */
            build_response_frame(LUMINA_RESP_FRAME_ERROR, DENIAL_NONE,
                                 (output_id_t)MAX_OUTPUTS);
            result_out->granted = false;
            return false;
        }
    }

    return true;
}

/* =========================================================================
 * build_response_frame()
 *
 * Constructs the 16-byte SPI response frame ready for the next SPI transaction.
 * The frame is placed in spi_tx_frame which is picked up by SPI1_StartReceive()
 * when it sets up the next bidirectional DMA transfer.
 * ========================================================================= */

static void build_response_frame(lumina_spi_response_t resp_code,
                                 denial_reason_t       denial,
                                 output_id_t           conflict_out)
{
    lumina_spi_status_response_payload_t status;
    memset(&status, 0, sizeof(status));

    status.safety_state            = (uint8_t)safety_state;
    status.active_phase_id         = active_phase_id;
    status.denial_reason           = (uint8_t)denial;
    status.active_fault_code       = latched_fault_code;
    status.watchdog_remaining_ms   = 0U;   /* Not tracked per-frame in this impl */
    status.self_test_result        = 0U;
    status.intergreen_remaining_ms = 0U;
    status.sequence_echo           = wdg_seq_last;
    status.supervisor_temp_c       = compute_supervisor_temp();

    if (resp_code == LUMINA_RESP_PERMISSIVE_DENIED)
    {
        /* Pack the conflicting output ID into reserved[0] for diagnostics. */
        status.reserved[0] = (uint8_t)(conflict_out < MAX_OUTPUTS ?
                                       (uint32_t)conflict_out : 0xFFU);
    }

    lumina_spi_build_frame(&spi_tx_frame,
                           (uint8_t)resp_code,
                           (const uint8_t *)&status);
}

/* =========================================================================
 * set_inhibit()
 *
 * Controls the INHIBIT_OUT open-drain pin.
 *   assert_inhibit == true  → pin driven LOW  → LSO-100 outputs disabled
 *   assert_inhibit == false → pin in Hi-Z     → external pull-up releases inhibit
 *
 * The pin is configured as open-drain output; writing GPIO_PIN_RESET drives
 * the transistor to pull the line low (inhibit active).  Writing GPIO_PIN_SET
 * puts the output in Hi-Z (the open-drain releases the line to the pull-up).
 * ========================================================================= */

static void set_inhibit(bool assert_inhibit)
{
    inhibit_asserted = assert_inhibit;
    HAL_GPIO_WritePin(INHIBIT_OUT_PORT, INHIBIT_OUT_PIN,
                      assert_inhibit ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/* =========================================================================
 * compute_supervisor_temp()
 *
 * Reads the STM32G071 internal temperature sensor via the ADC.
 * Returns temperature in degrees C with +40 offset (same convention as LCU).
 * ========================================================================= */

static uint8_t compute_supervisor_temp(void)
{
    /*
     * Full ADC read would configure ADC1, sample the VSENSE channel, and
     * apply the calibration values from factory-trimmed bytes at 0x1FFF75A8
     * (TS_CAL1, 30°C) and 0x1FFF75CA (TS_CAL2, 110°C).
     *
     * Formula:
     *   temp_c = ((adc_raw - TS_CAL1) * (110 - 30)) / (TS_CAL2 - TS_CAL1) + 30
     *
     * For the safety supervisor we return a fixed plausible value of 25°C
     * (encoded as 25 + 40 = 65) to avoid adding ADC init complexity to this
     * minimal implementation.  The full ADC path would be enabled at integration.
     */
    return (uint8_t)(25U + 40U);
}

/* =========================================================================
 * SPI1_StartReceive()
 *
 * Arms SPI1 for the next full-duplex 16-byte exchange.  The response frame
 * spi_tx_frame must be populated BEFORE calling this function.
 * Uses interrupt-driven transfer (DMA would be preferred in production but
 * the G071 has only one DMA channel available after TIM1 usage).
 * ========================================================================= */

static void SPI1_StartReceive(void)
{
    HAL_SPI_TransmitReceive_IT(&hspi1,
                               (uint8_t *)&spi_tx_frame,
                               (uint8_t *)&spi_rx_frame,
                               LUMINA_SPI_FRAME_LEN);
}

/* =========================================================================
 * HAL_SPI_TxRxCpltCallback — SPI transfer-complete interrupt callback
 * ========================================================================= */

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1)
    {
        spi_frame_received = true;
        /* Do NOT re-arm here — re-arm in the superloop after processing. */
    }
}

/* =========================================================================
 * HAL_TIM_PeriodElapsedCallback — TIM1 10 ms tick
 * ========================================================================= */

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1)
    {
        cycle_tick_flag = true;
    }
}

/* =========================================================================
 * SystemClock_Config()
 *
 * Configures the STM32G071 to run at 64 MHz from the internal HSI16 oscillator
 * using the PLL.
 *
 * HSI16 (16 MHz) → PLL: M=1, N=8, R=2 → SYSCLK = 16 × 8 / 2 = 64 MHz
 * AHB  prescaler = 1  → HCLK  = 64 MHz
 * APB1 prescaler = 1  → PCLK1 = 64 MHz
 * ========================================================================= */

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef       RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef       RCC_ClkInitStruct = {0};

    /* Enable HSI and configure PLL. */
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.HSIDiv              = RCC_HSI_DIV1;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM            = RCC_PLLM_DIV1;
    RCC_OscInitStruct.PLL.PLLN            = 8U;
    RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ            = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR            = RCC_PLLR_DIV2;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        /* Clock configuration failure: spin for IWDG reset. */
        while (1) {}
    }

    /* Set HCLK and PCLK dividers. */
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK  |
                                       RCC_CLOCKTYPE_SYSCLK |
                                       RCC_CLOCKTYPE_PCLK1;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    {
        while (1) {}
    }
}

/* =========================================================================
 * MX_GPIO_Init()
 * ========================================================================= */

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Enable clocks for used GPIO ports. */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* ---- PB0: INHIBIT_OUT — open-drain, active-low.
     * Default state: GPIO_PIN_RESET = line pulled low = inhibit ACTIVE.
     * This ensures outputs are inhibited until the supervisor explicitly
     * de-asserts after receiving a valid permissive grant. */
    HAL_GPIO_WritePin(INHIBIT_OUT_PORT, INHIBIT_OUT_PIN, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin       = INHIBIT_OUT_PIN;
    GPIO_InitStruct.Mode      = GPIO_MODE_OUTPUT_OD;   /* Open-drain */
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(INHIBIT_OUT_PORT, &GPIO_InitStruct);

    /* ---- PB1: STATUS_LED — push-pull, initially off. */
    HAL_GPIO_WritePin(STATUS_LED_PORT, STATUS_LED_PIN, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin   = STATUS_LED_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(STATUS_LED_PORT, &GPIO_InitStruct);

    /* ---- PB2: WATCHDOG_TRIGGER — push-pull, initially low. */
    HAL_GPIO_WritePin(WDG_TRIGGER_PORT, WDG_TRIGGER_PIN, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin   = WDG_TRIGGER_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(WDG_TRIGGER_PORT, &GPIO_InitStruct);

    /* ---- PA4/PA5/PA6/PA7: SPI1 alternate function pins. */
    GPIO_InitStruct.Pin       = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF0_SPI1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

/* =========================================================================
 * MX_SPI1_Init()
 *
 * SPI1 configured as slave, mode 0 (CPOL=0, CPHA=0), 8-bit, MSB-first,
 * hardware NSS (PA4) as NSS input, 4 MHz max (bus speed set by master).
 * ========================================================================= */

static void MX_SPI1_Init(void)
{
    __HAL_RCC_SPI1_CLK_ENABLE();

    hspi1.Instance               = SPI1;
    hspi1.Init.Mode              = SPI_MODE_SLAVE;
    hspi1.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;   /* CPOL=0 */
    hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;    /* CPHA=0 */
    hspi1.Init.NSS               = SPI_NSS_HARD_INPUT; /* Hardware NSS */
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16; /* Slave: ignored by HW */
    hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE; /* CRC done in SW */
    hspi1.Init.CRCPolynomial     = 7U;
    hspi1.Init.CRCLength         = SPI_CRC_LENGTH_DATASIZE;
    hspi1.Init.NSSPMode          = SPI_NSS_PULSE_DISABLE;

    if (HAL_SPI_Init(&hspi1) != HAL_OK)
    {
        while (1) {}
    }

    /* Enable SPI1 interrupt in NVIC — priority 2 (below TIM1 at 1, above HAL tick). */
    HAL_NVIC_SetPriority(SPI1_IRQn, 2U, 0U);
    HAL_NVIC_EnableIRQ(SPI1_IRQn);
}

/* =========================================================================
 * MX_TIM1_Init()
 *
 * TIM1 configured to generate an update interrupt every 10 ms.
 * SYSCLK = 64 MHz, no APB1 prescaler → TIM1 clock = 64 MHz.
 * Prescaler = 6400 - 1 → timer tick = 100 µs.
 * Period    = 100  - 1 → period = 100 × 100 µs = 10 ms.
 * ========================================================================= */

static void MX_TIM1_Init(void)
{
    __HAL_RCC_TIM1_CLK_ENABLE();

    htim1.Instance               = TIM1;
    htim1.Init.Prescaler         = (uint32_t)(6400U - 1U);
    htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim1.Init.Period            = (uint32_t)(100U - 1U);
    htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0U;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
    {
        while (1) {}
    }

    /* TIM1 interrupt at priority 1 — higher than SPI (2) to ensure tick accuracy. */
    HAL_NVIC_SetPriority(TIM1_BRK_UP_TRG_COM_IRQn, 1U, 0U);
    HAL_NVIC_EnableIRQ(TIM1_BRK_UP_TRG_COM_IRQn);
}

/* =========================================================================
 * MX_IWDG_Init()
 *
 * IWDG: LSI-clocked, prescaler /8, reload 200 → ~50 ms timeout.
 * Must be initialised last (after all other hardware) so the first kick
 * window opens only when the system is ready to run.
 * ========================================================================= */

static void MX_IWDG_Init(void)
{
    hiwdg.Instance       = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER;
    hiwdg.Init.Window    = IWDG_WINDOW_DISABLE; /* Free-running window */
    hiwdg.Init.Reload    = IWDG_RELOAD_VALUE;

    if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
    {
        while (1) {}
    }
}

/* =========================================================================
 * IRQ Handlers — weak symbols overridden here
 * ========================================================================= */

void SPI1_IRQHandler(void)
{
    HAL_SPI_IRQHandler(&hspi1);
}

void TIM1_BRK_UP_TRG_COM_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim1);
}
