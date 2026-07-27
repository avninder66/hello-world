/**
 * @file    fault_log.h
 * @brief   FRAM fault log driver header for the Lumina LCU-100.
 *
 * Provides a circular, append-only fault log stored in the 4 Mbit (512 KB)
 * FRAM device connected to the STM32H743 via SPI or I2C.  Entries survive
 * power cycles and MCU resets, giving a persistent fault history for field
 * diagnostics and post-incident analysis.
 *
 * Memory layout (FRAM address space, 0x00000–0x7FFFF):
 *
 *   0x00000 – 0x0001F  : fault_log_header_t  (32 bytes)
 *   0x00020 – 0x7FFFF  : fault_log_entry_t[] ring buffer
 *                        (0x7FFE0 / 32 = 16383 entries maximum)
 *
 * Each entry is exactly 32 bytes.  The header is re-written on every append
 * to update write_index, sequence_num, and CRC32.
 *
 * Severity ordering:
 *   FAULT_SEV_INFO < FAULT_SEV_WARNING < FAULT_SEV_CRITICAL < FAULT_SEV_SAFETY
 *
 * Copyright (c) Lumina Traffic Systems Ltd.  All rights reserved.
 * SPDX-License-Identifier: Proprietary
 */

#ifndef FAULT_LOG_H
#define FAULT_LOG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * FRAM memory map constants
 * ========================================================================= */

/** Base address of the FRAM device (byte-addressable). */
#define FRAM_BASE_ADDRESS       0x00000UL

/** Total FRAM capacity: 4 Mbit = 512 KiB. */
#define FRAM_SIZE               0x80000UL

/** Byte address of the fault log header within FRAM. */
#define FAULT_LOG_HEADER_ADDR   FRAM_BASE_ADDRESS

/** Byte address of the first fault log entry (after the 32-byte header). */
#define FAULT_LOG_DATA_ADDR     (FAULT_LOG_HEADER_ADDR + 32UL)

/** Maximum number of entries that fit in the FRAM data area. */
#define FAULT_LOG_MAX_ENTRIES   ((FRAM_SIZE - FAULT_LOG_DATA_ADDR) / 32UL)

/* =========================================================================
 * Magic and version constants
 * ========================================================================= */

/**
 * Magic number stored in fault_log_header_t.magic.
 * ASCII "LUMI" = 0x4C554D49.
 */
#define FAULT_LOG_MAGIC         0x4C554D49UL

/** Current log format version. */
#define FAULT_LOG_VERSION       0x0001U

/* =========================================================================
 * Fault severity codes
 * ========================================================================= */

typedef enum
{
    FAULT_SEV_INFO      = 0x00U,    /**< Informational event (no action required)   */
    FAULT_SEV_WARNING   = 0x01U,    /**< Degraded operation; monitor closely         */
    FAULT_SEV_CRITICAL  = 0x02U,    /**< Non-safety fault; outputs may be inhibited  */
    FAULT_SEV_SAFETY    = 0x03U,    /**< Safety fault; full lockout initiated        */
} fault_severity_t;

/* =========================================================================
 * Fault log entry structure
 *
 * Fixed size: exactly 32 bytes (enforced by _Static_assert below).
 * All multi-byte fields are stored little-endian (native ARM byte order).
 * ========================================================================= */

#pragma pack(push, 1)

/**
 * @brief One entry in the fault log ring buffer.
 *
 * Size: 32 bytes.
 */
typedef struct
{
    uint32_t    sequence_num;       /**< Monotonically incrementing log sequence    */
    uint32_t    timestamp_rtc;      /**< RTC seconds since Unix epoch (or 0 if no RTC) */
    uint16_t    fault_code;         /**< Fault code (fault_codes.h FAULT_xxx value) */
    uint8_t     severity;           /**< fault_severity_t                           */
    uint8_t     board_id;           /**< LUMINA_CAN_BASE_xxx of reporting board      */
    uint8_t     phase_id;           /**< Active phase when fault occurred (0xFF=N/A)*/
    uint8_t     extra_data[3];      /**< Board-specific diagnostic bytes            */
    uint8_t     description_idx;    /**< Index into firmware fault description table*/
    uint8_t     padding[18];        /**< Zero-pad to 32 bytes                       */
} fault_log_entry_t;

/**
 * @brief Fault log header stored at the start of FRAM.
 *
 * Size: 32 bytes.
 */
typedef struct
{
    uint32_t    magic;              /**< FAULT_LOG_MAGIC (0x4C554D49)               */
    uint16_t    version;            /**< FAULT_LOG_VERSION                          */
    uint16_t    reserved0;          /**< Must be 0x0000                             */
    uint32_t    write_index;        /**< Next entry write position (0-based ring)   */
    uint32_t    read_index;         /**< Oldest unread entry position               */
    uint32_t    total_entries;      /**< Cumulative entries written (never wraps)   */
    uint32_t    crc32;              /**< CRC-32/ISO-HDLC of header bytes [0..27]    */
} fault_log_header_t;

#pragma pack(pop)

/* Compile-time size assertions. */
_Static_assert(sizeof(fault_log_entry_t)  == 32U, "fault_log_entry_t must be 32 bytes");
_Static_assert(sizeof(fault_log_header_t) == 32U, "fault_log_header_t must be 32 bytes");

/* =========================================================================
 * Return codes
 * ========================================================================= */

typedef enum
{
    FAULT_LOG_OK            = 0,    /**< Operation succeeded                        */
    FAULT_LOG_ERR_INIT      = -1,   /**< Initialisation failed (FRAM not responding)*/
    FAULT_LOG_ERR_CRC       = -2,   /**< Header CRC mismatch on read                */
    FAULT_LOG_ERR_EMPTY     = -3,   /**< Log is empty; no entries to read           */
    FAULT_LOG_ERR_FULL      = -4,   /**< Log ring buffer full (oldest overwritten)  */
    FAULT_LOG_ERR_BOUNDS    = -5,   /**< Requested index is out of range            */
    FAULT_LOG_ERR_PARAM     = -6,   /**< NULL pointer or invalid parameter          */
    FAULT_LOG_ERR_IO        = -7,   /**< FRAM read/write I/O error                  */
} fault_log_result_t;

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * @brief Initialise the fault log module.
 *
 * Reads the FRAM header and validates the magic number and CRC.  If the header
 * is corrupt or uninitialised, the log is reformatted (header written, counters
 * zeroed).  Must be called once during system startup before any other
 * fault_log_* functions.
 *
 * @return FAULT_LOG_OK on success, FAULT_LOG_ERR_INIT if FRAM is unresponsive.
 */
fault_log_result_t fault_log_init(void);

/**
 * @brief Append one entry to the fault log ring buffer.
 *
 * Writes the entry to FRAM at the current write_index, then updates the header.
 * If the ring buffer is full (write_index wraps past read_index), the oldest
 * entry is silently overwritten (circular log).
 *
 * Caller must fill all fields of @p entry before calling.  sequence_num is
 * assigned automatically by this function.
 *
 * @param entry  Pointer to a populated fault_log_entry_t (sequence_num is ignored
 *               and overwritten).
 * @return       FAULT_LOG_OK on success, FAULT_LOG_ERR_IO on FRAM write error,
 *               FAULT_LOG_ERR_PARAM if entry is NULL.
 */
fault_log_result_t fault_log_write_entry(const fault_log_entry_t *entry);

/**
 * @brief Read one entry from the fault log by absolute sequence number.
 *
 * Searches the ring buffer for the entry whose sequence_num matches @p seq_num.
 * Sequence numbers are monotonically increasing; the oldest available entry
 * has the lowest sequence number.
 *
 * @param seq_num    Absolute sequence number to retrieve.
 * @param entry_out  Pointer to caller-supplied fault_log_entry_t to fill.
 * @return           FAULT_LOG_OK on success,
 *                   FAULT_LOG_ERR_BOUNDS if the entry has been overwritten,
 *                   FAULT_LOG_ERR_EMPTY if the log contains no entries.
 */
fault_log_result_t fault_log_read_entry(uint32_t seq_num, fault_log_entry_t *entry_out);

/**
 * @brief Return the number of entries currently in the ring buffer.
 *
 * This is the number of valid readable entries, capped at FAULT_LOG_MAX_ENTRIES.
 * Entries older than FAULT_LOG_MAX_ENTRIES writes ago have been overwritten.
 *
 * @return  Number of readable entries (0 if log is empty).
 */
uint32_t fault_log_get_count(void);

/**
 * @brief Erase all entries and reset the fault log to a clean state.
 *
 * Rewrites the header with zeroed counters and a fresh magic/CRC.  Does NOT
 * erase individual entry cells (FRAM is byte-rewritable; old data is merely
 * unreachable through the updated header).
 *
 * Requires operator authorisation: @p auth_key must be 0xDEADBEEF.
 *
 * @param auth_key  Authorisation key; must equal 0xDEADBEEFUL.
 * @return          FAULT_LOG_OK on success, FAULT_LOG_ERR_PARAM if key is wrong.
 */
fault_log_result_t fault_log_clear(uint32_t auth_key);

/**
 * @brief Dump all entries in the ring buffer over UART for diagnostics.
 *
 * Iterates from the oldest readable entry to the most recent and prints each
 * entry as a human-readable text line over the LCU-100 debug UART (USART3).
 * Output format per line:
 *   [NNNNNN] RTC=XXXXXXXXX SEV=X BRD=0xXX PH=XX FC=0xXXXX EXTRA=XX:XX:XX  DESCR=XXX
 *
 * This function is blocking and intended for use during startup diagnostics
 * or operator-initiated log review (not called in normal operation).
 *
 * @param max_entries  Maximum entries to print (0 = all).
 */
void fault_log_dump_uart(uint32_t max_entries);

/* =========================================================================
 * Convenience helper: build and write a fault entry in one call
 * ========================================================================= */

/**
 * @brief Construct and write a fault log entry from individual fields.
 *
 * Helper that populates a fault_log_entry_t and calls fault_log_write_entry().
 *
 * @param fault_code      Fault code (FAULT_xxx from fault_codes.h).
 * @param severity        fault_severity_t severity level.
 * @param board_id        LUMINA_CAN_BASE_xxx of the reporting board.
 * @param phase_id        Active phase index (0xFF if not applicable).
 * @param extra0          Extra diagnostic byte 0.
 * @param extra1          Extra diagnostic byte 1.
 * @param extra2          Extra diagnostic byte 2.
 * @param description_idx Index into the firmware fault description string table.
 * @return                FAULT_LOG_OK on success.
 */
fault_log_result_t fault_log_record(uint16_t      fault_code,
                                    fault_severity_t severity,
                                    uint8_t       board_id,
                                    uint8_t       phase_id,
                                    uint8_t       extra0,
                                    uint8_t       extra1,
                                    uint8_t       extra2,
                                    uint8_t       description_idx);

#ifdef __cplusplus
}
#endif

#endif /* FAULT_LOG_H */
