/**
 * @file    safety_comm.h
 * @brief   LCU-100 Safety Supervisor Communication — public interface
 *
 * The safety_comm module owns the SPI2 bus to the STM32G071 safety MCU.
 * It runs at the highest application task priority (below hardware ISRs)
 * with a strict 10 ms periodic budget.
 *
 * Thread safety
 * -------------
 * All public functions are safe to call from any task context.
 * Internal SPI access is serialised through xSPIMutex.
 *
 * Failure model
 * -------------
 * Three consecutive missed/invalid responses → SYSEVT_FAULT_ACTIVE and
 * INHIBIT_LINE asserted.  Recovery requires safety_comm_trigger_self_test()
 * to succeed, which clears the fault.
 */

#ifndef __SAFETY_COMM_H
#define __SAFETY_COMM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "lumina_safety_protocol.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* ============================================================================
 * Timing constants
 * ============================================================================ */

/** SPI transaction timeout in milliseconds */
#define SAFETY_SPI_TIMEOUT_MS       10U

/** Heartbeat frame period in milliseconds — must match safety MCU expectation */
#define SAFETY_HEARTBEAT_PERIOD_MS  10U

/** Consecutive failures before declaring comms fault */
#define SAFETY_MAX_MISS_COUNT       3U

/** Self-test completion poll period (ms) */
#define SAFETY_SELF_TEST_POLL_MS    50U

/** Maximum time to wait for self-test to complete (ms) */
#define SAFETY_SELF_TEST_TIMEOUT_MS 5000U

/* ============================================================================
 * Safety communication status snapshot
 *
 * Written by safety_comm_task on every successful exchange; readable by
 * other tasks via safety_comm_get_status() (mutex-protected copy).
 * ============================================================================ */
typedef struct {
    /** Current reported state of the safety MCU */
    safety_mcu_state_t state;

    /** Result of the most recent permissive request */
    permissive_result_t last_permissive_result;

    /** Denial reason code from the last denied permissive request */
    uint8_t denial_reason;

    /** Running count of successful SPI exchanges since startup */
    uint32_t watchdog_count;

    /** Fault flags received from safety MCU (SAFETY_FAULT_xxx bitmask) */
    uint16_t fault_flags;

    /** Most recent self-test result (0 = pass, non-zero = fail bitmask) */
    uint8_t self_test_result;

    /** Number of consecutive missed responses (resets to 0 on success) */
    uint8_t miss_count;
} safety_status_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * @brief  Initialise SPI2 CS/reset GPIO state and create xSPIMutex.
 *
 *         Must be called before vTaskStartScheduler().
 *
 * @return true on success.
 */
bool safety_comm_init(void);

/**
 * @brief  FreeRTOS task entry point — 10 ms SPI heartbeat loop.
 *
 *         Registered in main.c as "SafetyComm" at configMAX_PRIORITIES - 1.
 *
 * @param pvParameters  Unused; pass NULL.
 */
void safety_comm_task(void *pvParameters);

/**
 * @brief  Request a permissive for the specified phase from the safety MCU.
 *
 *         Sends a SAFETY_OP_REQUEST_PHASE frame over SPI and returns the
 *         result synchronously (blocks for up to SAFETY_SPI_TIMEOUT_MS * 3).
 *
 *         May be called from any task.  Uses xSPIMutex internally.
 *
 * @param[in] requested_phase  Phase ID to request permissive for (1-based).
 * @return    permissive_result_t result code.
 */
permissive_result_t safety_comm_request_permissive(uint8_t requested_phase);

/**
 * @brief  Return a snapshot of the current safety MCU status.
 *
 *         Thread-safe copy protected by xSPIMutex.
 *
 * @param[out] status_out  Caller-allocated struct to fill.
 */
void safety_comm_get_status(safety_status_t *status_out);

/**
 * @brief  Trigger a safety MCU self-test sequence.
 *
 *         Sends SAFETY_OP_SELF_TEST and polls until the result is available
 *         (up to SAFETY_SELF_TEST_TIMEOUT_MS).  Clears the fault active bit
 *         if the test passes.
 *
 * @return true if self-test passed, false on failure or timeout.
 */
bool safety_comm_trigger_self_test(void);

/**
 * @brief  Immediately assert the inhibit line via software command to safety MCU.
 *
 *         Sends SAFETY_OP_INHIBIT frame; does not wait for a response.
 *         GPIO inhibit line is also driven directly from traffic_engine.c.
 */
void safety_comm_emergency_inhibit(void);

/* ============================================================================
 * Shared mutex (extern so traffic_engine.c can guard CAN + SPI together)
 * ============================================================================ */
extern SemaphoreHandle_t xSPIMutex;

#ifdef __cplusplus
}
#endif

#endif /* __SAFETY_COMM_H */
