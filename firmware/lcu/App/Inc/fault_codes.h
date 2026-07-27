/**
 * @file    fault_codes.h
 * @brief   LCU-100 fault code definitions (Lumina platform)
 *
 * Fault codes are stored in FRAM, transmitted in telemetry, and displayed
 * on the front-panel LCD.  Codes 0x0000 are reserved (no fault).
 * Codes in the 0xF000 range are fatal and force FAULT_LOCKOUT.
 */

#ifndef __FAULT_CODES_H
#define __FAULT_CODES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* --------------------------------------------------------------------------
 * Fault severity
 * -------------------------------------------------------------------------- */
typedef enum {
    FAULT_SEV_INFO      = 0,    /**< Informational, no operational impact        */
    FAULT_SEV_WARNING   = 1,    /**< Degraded operation, log and continue        */
    FAULT_SEV_CRITICAL  = 2,    /**< Inhibit phase change until cleared          */
    FAULT_SEV_FATAL     = 3,    /**< Force FAULT_LOCKOUT, require manual reset   */
} fault_severity_t;

/* --------------------------------------------------------------------------
 * Fault source subsystem
 * -------------------------------------------------------------------------- */
typedef enum {
    FAULT_SRC_SYSTEM        = 0x00,
    FAULT_SRC_TRAFFIC_ENGINE = 0x01,
    FAULT_SRC_SAFETY_COMM   = 0x02,
    FAULT_SRC_CAN_BUS       = 0x03,
    FAULT_SRC_POWER         = 0x04,
    FAULT_SRC_GNSS          = 0x05,
    FAULT_SRC_TELEMETRY     = 0x06,
    FAULT_SRC_FRAM          = 0x07,
    FAULT_SRC_LSO           = 0x08,
} fault_source_t;

/* --------------------------------------------------------------------------
 * Fault code enumeration
 * -------------------------------------------------------------------------- */
typedef enum {
    /* System / startup faults */
    FAULT_NONE                      = 0x0000,
    FAULT_WATCHDOG_RESET            = 0x0001,
    FAULT_STACK_OVERFLOW            = 0x0002,
    FAULT_HARDFAULT                 = 0x0003,
    FAULT_CLOCK_DRIFT               = 0x0004,

    /* Safety supervisor faults */
    FAULT_SAFETY_COMM_TIMEOUT       = 0x0100,
    FAULT_SAFETY_COMM_CRC           = 0x0101,
    FAULT_SAFETY_PERMISSIVE_DENIED  = 0x0102,
    FAULT_SAFETY_SELF_TEST_FAIL     = 0x0103,
    FAULT_SAFETY_WATCHDOG_EXPIRED   = 0x0104,

    /* Traffic engine faults */
    FAULT_PHASE_PLAN_INVALID        = 0x0200,
    FAULT_PHASE_TIMEOUT             = 0x0201,
    FAULT_PHASE_SEQ_ERROR           = 0x0202,
    FAULT_NO_PHASE_PLAN             = 0x0203,

    /* CAN bus faults */
    FAULT_CAN_BUS_OFF               = 0x0300,
    FAULT_CAN_LSO_TIMEOUT           = 0x0301,
    FAULT_CAN_TX_OVERFLOW           = 0x0302,

    /* Power faults */
    FAULT_BATTERY_LOW               = 0x0400,
    FAULT_BATTERY_CRITICAL          = 0x0401,
    FAULT_OVERCURRENT               = 0x0402,
    FAULT_OVERVOLTAGE               = 0x0403,

    /* FRAM / storage faults */
    FAULT_FRAM_WRITE_FAIL           = 0x0700,
    FAULT_FRAM_READ_FAIL            = 0x0701,
    FAULT_FRAM_CRC_MISMATCH         = 0x0702,

    /* LSO (Lane Signal Output) faults */
    FAULT_LSO_NOT_RESPONDING        = 0x0800,
    FAULT_LSO_ASPECT_MISMATCH       = 0x0801,
    FAULT_LSO_LAMP_FAIL             = 0x0802,

    /* Fatal faults (force lockout) */
    FAULT_FATAL_INHIBIT_STUCK       = 0xF001,
    FAULT_FATAL_DUAL_MCU_DISAGREE   = 0xF002,
    FAULT_FATAL_MEMORY_CORRUPT      = 0xF003,
} fault_code_t;

/* --------------------------------------------------------------------------
 * Fault event record (written to FRAM and placed on xFaultQueue)
 * -------------------------------------------------------------------------- */
typedef struct {
    fault_code_t     code;
    fault_severity_t severity;
    fault_source_t   source;
    uint32_t         timestamp_ms;      /**< HAL_GetTick() at fault occurrence   */
    uint32_t         context[2];        /**< Source-specific diagnostic data     */
} fault_event_t;

#ifdef __cplusplus
}
#endif

#endif /* __FAULT_CODES_H */
