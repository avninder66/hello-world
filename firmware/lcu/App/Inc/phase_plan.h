/**
 * @file    phase_plan.h
 * @brief   LCU-100 Phase Plan data structures (Lumina platform)
 *
 * A phase plan describes the complete timing and sequencing programme
 * for one operational period.  It is either loaded from FRAM at startup
 * or downloaded over CAN from the Traffic Management Centre.
 */

#ifndef __PHASE_PLAN_H
#define __PHASE_PLAN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* --------------------------------------------------------------------------
 * Limits
 * -------------------------------------------------------------------------- */
#define PHASE_PLAN_MAX_PHASES       16U     /**< Maximum phase count per plan   */
#define PHASE_PLAN_MAX_APPROACHES   8U      /**< Maximum approaches (LSOs)      */
#define PHASE_PLAN_ID_LEN           32U     /**< Plan identifier string length  */

/* --------------------------------------------------------------------------
 * Aspect / signal head outputs
 * -------------------------------------------------------------------------- */
typedef enum {
    ASPECT_OFF          = 0x00,
    ASPECT_RED          = 0x01,
    ASPECT_AMBER        = 0x02,
    ASPECT_GREEN        = 0x03,
    ASPECT_FLASHING_AMBER = 0x04,
    ASPECT_FLASHING_GREEN = 0x05,
} signal_aspect_t;

/* --------------------------------------------------------------------------
 * Single phase descriptor
 * -------------------------------------------------------------------------- */
typedef struct {
    uint8_t  phase_id;                              /**< Phase number, 1-based                      */
    uint8_t  num_approaches;                        /**< How many approaches are defined             */
    uint8_t  approach_aspects[PHASE_PLAN_MAX_APPROACHES]; /**< aspect per approach (signal_aspect_t) */
    uint32_t min_green_ms;                          /**< Minimum green time before phase can change  */
    uint32_t max_green_ms;                          /**< Maximum green time (forced phase change)    */
    uint32_t intergreen_before_ms;                  /**< Amber clearance duration entering phase     */
    uint32_t all_red_after_ms;                      /**< All-red clearance after amber               */
    uint8_t  next_phase_default;                    /**< Default successor phase_id (round-robin)    */
    bool     demand_enabled;                        /**< Accept vehicle demand for early phase change */
} phase_descriptor_t;

/* --------------------------------------------------------------------------
 * Complete phase plan
 * -------------------------------------------------------------------------- */
typedef struct {
    char     plan_id[PHASE_PLAN_ID_LEN];            /**< Human-readable plan name                   */
    uint32_t plan_version;                          /**< Incremented on every TMC upload             */
    uint8_t  num_phases;                            /**< Number of valid entries in phases[]         */
    uint8_t  start_phase_id;                        /**< Phase to enter first after startup          */
    uint32_t all_red_startup_ms;                    /**< All-red dwell at power-on                   */
    phase_descriptor_t phases[PHASE_PLAN_MAX_PHASES];
    uint16_t crc16;                                 /**< CRC-16/CCITT of all fields except this one  */
} phase_plan_t;

#ifdef __cplusplus
}
#endif

#endif /* __PHASE_PLAN_H */
