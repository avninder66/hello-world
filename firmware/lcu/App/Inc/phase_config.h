/**
 * @file    phase_config.h
 * @brief   LCU-100 Phase Plan Configuration Loader — public interface
 *
 * Provides load/save/default functions for persisting a phase_plan_t in the
 * upper region of the MB85RS4MT FRAM (address 0x010000 upward), separate from
 * the fault log which occupies the lower region starting at 0x000000.
 *
 * FRAM layout (upper region, starting at PHASE_CONFIG_FRAM_ADDRESS)
 * -----------------------------------------------------------------
 *   0x010000  phase_config_block_t:
 *               [4]  magic    (PHASE_CONFIG_MAGIC = 0x504C414E)
 *               [1]  version_major
 *               [1]  version_minor
 *               [2]  reserved
 *               [N]  phase_plan_t payload
 *               [4]  crc32 over all preceding bytes in the block
 *
 * The wrapper struct is written and read atomically (FRAM has no page
 * boundaries, so a single SPI transaction covers the whole block).
 *
 * Recovery policy
 * ---------------
 * If reading the configuration fails for any reason (magic mismatch, version
 * incompatibility, CRC error, SPI failure), phase_config_init() falls back to
 * the built-in compile-time default plan (phase_plan_shuttle_2way from
 * firmware/common/config/phase_plan.h) and logs a CONFIG fault to xFaultQueue.
 * The controller continues to operate on the default plan.
 *
 * Thread safety
 * -------------
 * phase_config_load() and phase_config_save() both take xSPIMutex for the
 * duration of the FRAM SPI transaction.  They are intended to be called at
 * startup (before the traffic engine task runs) or from a service context
 * (manual override active).  Calling them while a phase is actively running
 * is safe but the new plan will not take effect until
 * traffic_engine_load_plan() is called with the updated plan pointer.
 */

#ifndef __PHASE_CONFIG_H
#define __PHASE_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "phase_plan.h"

/* ============================================================================
 * Storage address
 * ============================================================================ */

/**
 * FRAM byte address of the phase configuration block.
 * Placed in the upper FRAM region, well above the fault log ring buffer
 * (which occupies 0x000000 – 0x00FFFF at most).
 */
#define PHASE_CONFIG_FRAM_ADDRESS   0x010000UL

/* ============================================================================
 * Magic and version
 * ============================================================================ */

/**
 * Four-byte magic word identifying a valid phase configuration block.
 * ASCII "PLAN" in little-endian byte order: 0x4E, 0x41, 0x4C, 0x50.
 */
#define PHASE_CONFIG_MAGIC          0x504C414EUL

/** Major version of the stored configuration format */
#define PHASE_CONFIG_VERSION_MAJOR  1U

/** Minor version of the stored configuration format */
#define PHASE_CONFIG_VERSION_MINOR  0U

/* ============================================================================
 * Result codes
 * ============================================================================ */

typedef enum {
    CONFIG_OK               = 0,    /**< Plan loaded or saved successfully    */
    CONFIG_CRC_FAIL         = 1,    /**< CRC-32 mismatch in stored block      */
    CONFIG_MAGIC_FAIL       = 2,    /**< Magic word not found                 */
    CONFIG_VERSION_MISMATCH = 3,    /**< Stored version incompatible          */
    CONFIG_EMPTY            = 4,    /**< FRAM appears uninitialised (0xFF)    */
    CONFIG_SPI_ERROR        = 5,    /**< FRAM SPI transaction failed          */
} config_load_result_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * @brief  Initialise the phase configuration module and load the plan.
 *
 *         Attempts to load from FRAM.  On any failure, loads the compile-time
 *         default and logs a fault to xFaultQueue.  Calls
 *         traffic_engine_load_plan() with the resulting plan so the engine
 *         is ready to run immediately after this function returns.
 *
 *         Must be called after fault_log_init() (needs xFaultQueue) and
 *         before vTaskStartScheduler().
 *
 * @return CONFIG_OK if FRAM was read successfully, error code otherwise.
 *         In all cases the traffic engine receives a valid plan.
 */
config_load_result_t phase_config_init(void);

/**
 * @brief  Load the phase plan from FRAM into *plan.
 *
 *         Reads the configuration block, validates magic, version, and
 *         CRC-32, then extracts the embedded phase_plan_t.
 *
 * @param[out] plan  Caller-allocated phase_plan_t to fill on success.
 * @return CONFIG_OK on success, or an error code describing the failure.
 */
config_load_result_t phase_config_load(phase_plan_t *plan);

/**
 * @brief  Save a phase plan to FRAM.
 *
 *         Wraps the plan in a configuration block (magic + version + CRC-32)
 *         and writes it atomically to PHASE_CONFIG_FRAM_ADDRESS.
 *
 * @param[in] plan  Pointer to the plan to persist.
 * @return CONFIG_OK on success, CONFIG_SPI_ERROR if the SPI transaction fails.
 */
config_load_result_t phase_config_save(const phase_plan_t *plan);

/**
 * @brief  Populate *plan with the compile-time default shuttle plan.
 *
 *         Copies phase_plan_shuttle_2way (from phase_plan.h) into the
 *         caller's buffer.  Does not touch FRAM.
 *
 * @param[out] plan  Caller-allocated phase_plan_t to fill.
 */
void phase_config_load_default(phase_plan_t *plan);

#ifdef __cplusplus
}
#endif

#endif /* __PHASE_CONFIG_H */
