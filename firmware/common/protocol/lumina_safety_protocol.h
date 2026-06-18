/**
 * @file    lumina_safety_protocol.h
 * @brief   Safety supervisor inter-MCU SPI protocol for Lumina LCU-100.
 *
 * Defines the SPI framing, command opcodes, response codes, and safety state
 * machine states used for communication between the main application MCU
 * (STM32H743, SPI master) and the safety supervisor MCU (STM32G071, SPI slave)
 * on the LCU-100 board.
 *
 * Physical interface:
 *   - SPI mode 0 (CPOL=0, CPHA=0), 8-bit transfers, MSB first
 *   - Clock frequency: 4 MHz maximum
 *   - CS line is GPIO-controlled, asserted for the full 16-byte frame
 *   - Inter-frame gap: minimum 50 µs
 *
 * Frame structure (16 bytes):
 *   Byte  0     : Start-of-frame marker (LUMINA_SPI_SOF)
 *   Byte  1     : Opcode / response code (command or response)
 *   Bytes 2–13  : Payload (12 bytes, opcode-specific, unused bytes = 0x00)
 *   Byte 14     : CRC-8 over bytes 0–13
 *   Byte 15     : End-of-frame marker (LUMINA_SPI_EOF)
 *
 * Transaction model:
 *   The master transmits a 16-byte command frame simultaneously receiving a
 *   16-byte response frame from the slave (full-duplex).  The response reflects
 *   the supervisor's state BEFORE processing the new command.  The master must
 *   verify the response CRC and EOF marker before acting on the contents.
 *
 * Copyright (c) Lumina Traffic Systems Ltd.  All rights reserved.
 * SPDX-License-Identifier: Proprietary
 */

#ifndef LUMINA_SAFETY_PROTOCOL_H
#define LUMINA_SAFETY_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Frame constants
 * ========================================================================= */

#define LUMINA_SPI_FRAME_LEN        16U     /**< Total bytes per SPI transaction    */
#define LUMINA_SPI_PAYLOAD_LEN      12U     /**< Payload bytes within each frame    */
#define LUMINA_SPI_SOF              0xA5U   /**< Start-of-frame marker              */
#define LUMINA_SPI_EOF              0x5AU   /**< End-of-frame marker                */
#define LUMINA_SPI_IDLE_BYTE        0xFFU   /**< Bus idle / filler byte             */

/** Byte offsets within the 16-byte frame. */
#define LUMINA_SPI_OFFSET_SOF       0U
#define LUMINA_SPI_OFFSET_OPCODE    1U
#define LUMINA_SPI_OFFSET_PAYLOAD   2U
#define LUMINA_SPI_OFFSET_CRC       14U
#define LUMINA_SPI_OFFSET_EOF       15U

/** Inter-frame gap in microseconds (enforced by master before next CS assert). */
#define LUMINA_SPI_IFG_US           50U

/** Maximum consecutive CRC errors before master raises a link-loss fault. */
#define LUMINA_SPI_MAX_CRC_ERRORS   3U

/* =========================================================================
 * Command opcodes  (master → slave)
 * ========================================================================= */

typedef enum
{
    /**
     * Request permission to activate a traffic phase.
     * Payload: lumina_spi_req_phase_payload_t
     * Response: PERMISSIVE_GRANTED or PERMISSIVE_DENIED + denial reason.
     */
    LUMINA_CMD_REQUEST_PHASE    = 0x10U,

    /**
     * Confirm that the controller has placed all channels into inhibit/all-red
     * and requests the supervisor to latch the inhibit state.
     * Payload: lumina_spi_confirm_inhibit_payload_t
     * Response: PERMISSIVE_GRANTED (inhibit latched) or FAULT_ACTIVE.
     */
    LUMINA_CMD_CONFIRM_INHIBIT  = 0x11U,

    /**
     * Read the current supervisor status without changing state.
     * Payload: all zeros.
     * Response: lumina_spi_status_response_payload_t in the response frame.
     */
    LUMINA_CMD_READ_STATUS      = 0x12U,

    /**
     * Clear an acknowledged latched fault on the supervisor.
     * Payload: lumina_spi_clear_fault_payload_t (fault code to clear).
     * Response: PERMISSIVE_GRANTED (cleared) or FAULT_ACTIVE (still latched).
     */
    LUMINA_CMD_CLEAR_FAULT      = 0x13U,

    /**
     * Initiate a supervised self-test sequence.  The supervisor will exercise
     * conflict matrix logic and watchdog timers.  Outputs must be inhibited
     * before issuing this command.
     * Payload: lumina_spi_self_test_payload_t
     * Response: PERMISSIVE_GRANTED (test started) or FAULT_ACTIVE.
     */
    LUMINA_CMD_SELF_TEST        = 0x14U,

    /**
     * Keepalive / watchdog pet.  Must be sent within LUMINA_SAFETY_WDG_PERIOD_MS
     * or the supervisor will assert a watchdog fault.
     * Payload: rolling 8-bit counter in payload[0], all others zero.
     * Response: current safety state.
     */
    LUMINA_CMD_WATCHDOG_PET     = 0x15U,

    /**
     * Write a new conflict matrix to the supervisor's RAM.
     * Payload: lumina_spi_conflict_matrix_payload_t
     * Response: PERMISSIVE_GRANTED or FAULT_ACTIVE (matrix validation fail).
     */
    LUMINA_CMD_WRITE_CONFLICT   = 0x16U,
} lumina_spi_cmd_t;

/* =========================================================================
 * Response codes  (slave → master, in response frame opcode byte)
 * ========================================================================= */

typedef enum
{
    /**
     * Phase activation or inhibit operation is approved.
     * The supervisor has validated the conflict matrix and timing constraints.
     */
    LUMINA_RESP_PERMISSIVE_GRANTED  = 0x80U,

    /**
     * Phase activation is denied.  Denial reason is in payload[0].
     * See lumina_denial_reason_t.
     */
    LUMINA_RESP_PERMISSIVE_DENIED   = 0x81U,

    /**
     * Supervisor has an active latched fault.  Fault code in payload[1:2].
     * The system must not activate any phase until the fault is cleared.
     */
    LUMINA_RESP_FAULT_ACTIVE        = 0x82U,

    /**
     * Watchdog timer is within LUMINA_SAFETY_WDG_WARN_MS of expiry.
     * Master must pet the watchdog immediately.
     */
    LUMINA_RESP_WATCHDOG_WARNING    = 0x83U,

    /**
     * Self-test has completed.  Pass/fail result in payload[0].
     * Detailed result flags in payload[1].
     */
    LUMINA_RESP_SELF_TEST_COMPLETE  = 0x84U,

    /**
     * The received command frame had a CRC error or framing error.
     * Master must retransmit.  payload[0] = error type.
     */
    LUMINA_RESP_FRAME_ERROR         = 0x85U,

    /**
     * Echoed status response to READ_STATUS or WATCHDOG_PET commands.
     * Payload: lumina_spi_status_response_payload_t.
     */
    LUMINA_RESP_STATUS              = 0x86U,
} lumina_spi_response_t;

/* =========================================================================
 * Denial reason codes  (payload[0] when PERMISSIVE_DENIED)
 * ========================================================================= */

typedef enum
{
    /** Requested phase conflicts with an already-active phase in the matrix. */
    LUMINA_DENY_CONFLICT_MATRIX_FAIL    = 0x01U,

    /** Minimum intergreen or all-red timer has not yet elapsed. */
    LUMINA_DENY_TIMER_NOT_ELAPSED       = 0x02U,

    /** LSO-100 is reporting a channel fault that blocks this phase. */
    LUMINA_DENY_LSO_FAULT_ACTIVE        = 0x03U,

    /** Battery SoC is at or below the critical threshold. */
    LUMINA_DENY_BATTERY_CRITICAL        = 0x04U,

    /** CAN FD link to one or more required nodes has been lost. */
    LUMINA_DENY_LINK_LOSS               = 0x05U,

    /** Supervisor is in FAULT_LOCKOUT state and must be cleared first. */
    LUMINA_DENY_SUPERVISOR_FAULT        = 0x06U,

    /** The phase_id in the request does not exist in the loaded plan. */
    LUMINA_DENY_INVALID_PHASE_ID        = 0x07U,

    /** Watchdog has expired; outputs are locked until reset. */
    LUMINA_DENY_WATCHDOG_EXPIRED        = 0x08U,

    /** A self-test is currently running; no phase changes permitted. */
    LUMINA_DENY_SELF_TEST_ACTIVE        = 0x09U,

    /** All-red holdover timer running after previous phase completion. */
    LUMINA_DENY_ALL_RED_HOLDOVER        = 0x0AU,
} lumina_denial_reason_t;

/* =========================================================================
 * Safety state machine states
 * ========================================================================= */

typedef enum
{
    /**
     * Supervisor is initialising: loading NVM config, running ROM CRC,
     * checking RAM.  No outputs may be activated.
     */
    LUMINA_SAFETY_STATE_INIT            = 0x00U,

    /**
     * All outputs are held at red (or off for non-road approaches).
     * This is the default safe state and the state entered after any
     * phase deactivation or fault clearance.
     */
    LUMINA_SAFETY_STATE_ALL_RED         = 0x01U,

    /**
     * A phase has been approved by the supervisor and is now active.
     * The conflict matrix has been validated; the intergreen sequence
     * has completed.
     */
    LUMINA_SAFETY_STATE_PHASE_ACTIVE    = 0x02U,

    /**
     * A safety fault has been detected and latched.  All outputs are
     * unconditionally inhibited via hardware.  The system remains in
     * this state until the fault is acknowledged and the MCU is reset.
     */
    LUMINA_SAFETY_STATE_FAULT_LOCKOUT   = 0x03U,

    /**
     * Supervised self-test is running.  All outputs are inhibited.
     * Transitions back to ALL_RED on pass, FAULT_LOCKOUT on fail.
     */
    LUMINA_SAFETY_STATE_SELF_TEST       = 0x04U,

    /**
     * Intergreen transition sequence in progress between phases.
     * Outputs follow the intergreen channel map until all timers expire,
     * then transitions to PHASE_ACTIVE or ALL_RED.
     */
    LUMINA_SAFETY_STATE_INTERGREEN      = 0x05U,
} lumina_safety_state_t;

/* =========================================================================
 * Watchdog constants
 * ========================================================================= */

/** Watchdog period: master must pet within this interval. */
#define LUMINA_SAFETY_WDG_PERIOD_MS     200U

/** Warning threshold: WATCHDOG_WARNING response issued within this time of expiry. */
#define LUMINA_SAFETY_WDG_WARN_MS       50U

/** Number of watchdog expirations before FAULT_LOCKOUT (must be 1 for safety). */
#define LUMINA_SAFETY_WDG_MAX_MISS      1U

/* =========================================================================
 * SPI payload structures  (12 bytes each, packed)
 * ========================================================================= */

#pragma pack(push, 1)

/**
 * Payload for LUMINA_CMD_REQUEST_PHASE.
 */
typedef struct
{
    uint8_t     phase_id;               /**< Requested phase index (0-based)        */
    uint8_t     operating_mode;         /**< lumina_operating_mode_t value          */
    uint16_t    requested_duration_ms;  /**< Requested duration (0 = plan default)  */
    uint16_t    intergreen_override_ms; /**< 0 = use plan default                   */
    uint8_t     flags;                  /**< Bit 0: ped_green_requested             */
    uint8_t     sequence;               /**< Rolling counter, echoed in response    */
    uint8_t     reserved[4];            /**< Must be 0x00                           */
} lumina_spi_req_phase_payload_t;

/**
 * Payload for LUMINA_CMD_CONFIRM_INHIBIT.
 */
typedef struct
{
    uint8_t     reason;                 /**< lumina_inhibit_reason_t                */
    uint8_t     sequence;               /**< Rolling sequence from INHIBIT_CMD      */
    uint16_t    fault_code;             /**< Active fault code triggering inhibit   */
    uint8_t     reserved[8];            /**< Must be 0x00                           */
} lumina_spi_confirm_inhibit_payload_t;

/**
 * Payload for LUMINA_CMD_CLEAR_FAULT.
 */
typedef struct
{
    uint16_t    fault_code;             /**< Fault code to acknowledge and clear    */
    uint8_t     operator_key;           /**< Operator authorisation byte (0x55)     */
    uint8_t     reserved[9];            /**< Must be 0x00                           */
} lumina_spi_clear_fault_payload_t;

/**
 * Payload for LUMINA_CMD_SELF_TEST.
 */
typedef struct
{
    uint8_t     test_mask;              /**< Bitmask of tests to run (see below)    */
    uint8_t     reserved[11];           /**< Must be 0x00                           */
} lumina_spi_self_test_payload_t;

/** Self-test mask bits for lumina_spi_self_test_payload_t.test_mask. */
#define LUMINA_SELF_TEST_CONFLICT_MATRIX    (1U << 0U)  /**< Validate conflict matrix   */
#define LUMINA_SELF_TEST_WATCHDOG           (1U << 1U)  /**< Exercise WDG near-miss     */
#define LUMINA_SELF_TEST_RAM                (1U << 2U)  /**< Walk-pattern RAM test      */
#define LUMINA_SELF_TEST_FLASH_CRC          (1U << 3U)  /**< Verify firmware CRC        */
#define LUMINA_SELF_TEST_OUTPUT_LOOP        (1U << 4U)  /**< Drive and sense each chan  */
#define LUMINA_SELF_TEST_TIMER_ACCURACY     (1U << 5U)  /**< Verify intergreen timers   */
#define LUMINA_SELF_TEST_ALL                0x3FU

/**
 * Conflict matrix payload for LUMINA_CMD_WRITE_CONFLICT.
 * 12 bytes = 12×8 bits, one row per channel.
 * conflict_matrix[i] bit j set means channel i and channel j conflict.
 */
typedef struct
{
    uint16_t    row[6];                 /**< 6 × 16-bit rows covering 12 channels   */
} lumina_spi_conflict_matrix_payload_t;

/**
 * Status response payload returned by READ_STATUS and WATCHDOG_PET responses.
 * Carried in the response frame payload bytes [2..13].
 */
typedef struct
{
    uint8_t     safety_state;           /**< lumina_safety_state_t                  */
    uint8_t     active_phase_id;        /**< Currently approved phase (0xFF = none) */
    uint8_t     denial_reason;          /**< Last denial reason, 0 if none          */
    uint16_t    active_fault_code;      /**< Latched fault code, 0x0000 if none     */
    uint8_t     watchdog_remaining_ms;  /**< WDG countdown (clamped to 255 ms)      */
    uint8_t     self_test_result;       /**< Last self-test pass/fail flags         */
    uint8_t     intergreen_remaining_ms;/**< Intergreen timer countdown (0 = done)  */
    uint8_t     sequence_echo;          /**< Echo of last command sequence byte     */
    uint8_t     supervisor_temp_c;      /**< STM32G071 die temp (offset 40)         */
    uint8_t     reserved[2];            /**< Must be 0x00                           */
} lumina_spi_status_response_payload_t;

/**
 * Full 16-byte SPI frame (command or response).
 */
typedef struct
{
    uint8_t     sof;                    /**< LUMINA_SPI_SOF = 0xA5                  */
    uint8_t     opcode;                 /**< lumina_spi_cmd_t or lumina_spi_response_t */
    uint8_t     payload[LUMINA_SPI_PAYLOAD_LEN]; /**< Opcode-specific data         */
    uint8_t     crc8;                   /**< CRC-8/MAXIM over bytes [0..13]         */
    uint8_t     eof;                    /**< LUMINA_SPI_EOF = 0x5A                  */
} lumina_spi_frame_t;

#pragma pack(pop)

/* =========================================================================
 * Compile-time size assertions
 * ========================================================================= */

#ifndef __cplusplus
_Static_assert(sizeof(lumina_spi_frame_t)                    == 16U, "spi frame size");
_Static_assert(sizeof(lumina_spi_req_phase_payload_t)        == 12U, "req_phase payload");
_Static_assert(sizeof(lumina_spi_confirm_inhibit_payload_t)  == 12U, "confirm_inhibit payload");
_Static_assert(sizeof(lumina_spi_clear_fault_payload_t)      == 12U, "clear_fault payload");
_Static_assert(sizeof(lumina_spi_self_test_payload_t)        == 12U, "self_test payload");
_Static_assert(sizeof(lumina_spi_conflict_matrix_payload_t)  == 12U, "conflict_matrix payload");
_Static_assert(sizeof(lumina_spi_status_response_payload_t)  == 12U, "status_response payload");
#endif

/* =========================================================================
 * CRC-8 / MAXIM (polynomial 0x31, init 0x00, no input/output reflection)
 *
 * Used for frame integrity over bytes [SOF .. payload[11]] (14 bytes total).
 *
 * LUMINA_CRC8_UPDATE(crc, byte) — single-byte accumulation macro suitable
 * for use in a bare-metal context with no heap allocation.
 *
 * Usage:
 *   uint8_t crc = 0x00U;
 *   for (size_t i = 0; i < LUMINA_SPI_OFFSET_CRC; i++) {
 *       LUMINA_CRC8_UPDATE(crc, buf[i]);
 *   }
 * ========================================================================= */

#define LUMINA_CRC8_POLY    0x31U
#define LUMINA_CRC8_INIT    0x00U

/**
 * @brief Update a running CRC-8/MAXIM value with one byte.
 * @param crc   uint8_t accumulator variable (modified in-place).
 * @param byte  uint8_t data byte to incorporate.
 */
#define LUMINA_CRC8_UPDATE(crc, byte)                                   \
    do {                                                                 \
        uint8_t _b = (uint8_t)(byte);                                   \
        uint8_t _i;                                                      \
        (crc) ^= _b;                                                     \
        for (_i = 0U; _i < 8U; _i++)                                    \
        {                                                                \
            if ((crc) & 0x80U)                                          \
            {                                                            \
                (crc) = (uint8_t)(((crc) << 1U) ^ LUMINA_CRC8_POLY);   \
            }                                                            \
            else                                                         \
            {                                                            \
                (crc) = (uint8_t)((crc) << 1U);                         \
            }                                                            \
        }                                                                \
    } while (0)

/* =========================================================================
 * Inline frame construction / validation helpers
 * ========================================================================= */

/**
 * @brief Compute the CRC-8 over the first 14 bytes of a frame buffer.
 * @param buf   Pointer to a 16-byte frame buffer.
 * @return      CRC-8 value to be placed at buf[14].
 */
static inline uint8_t lumina_spi_compute_crc(const uint8_t buf[LUMINA_SPI_FRAME_LEN])
{
    uint8_t crc = LUMINA_CRC8_INIT;
    uint8_t i;
    for (i = 0U; i < LUMINA_SPI_OFFSET_CRC; i++)
    {
        LUMINA_CRC8_UPDATE(crc, buf[i]);
    }
    return crc;
}

/**
 * @brief Validate SOF, EOF, and CRC of a received frame.
 * @param frame Pointer to the received 16-byte frame.
 * @return      true if the frame is structurally valid.
 */
static inline bool lumina_spi_frame_valid(const lumina_spi_frame_t *frame)
{
    if (frame->sof != LUMINA_SPI_SOF) { return false; }
    if (frame->eof != LUMINA_SPI_EOF) { return false; }

    const uint8_t *buf = (const uint8_t *)frame;
    uint8_t crc = LUMINA_CRC8_INIT;
    uint8_t i;
    for (i = 0U; i < LUMINA_SPI_OFFSET_CRC; i++)
    {
        LUMINA_CRC8_UPDATE(crc, buf[i]);
    }
    return (crc == frame->crc8);
}

/**
 * @brief Initialise and seal a command frame ready for transmission.
 *
 * Fills SOF, opcode, copies payload bytes, computes and inserts the CRC,
 * and appends the EOF marker.
 *
 * @param frame    Destination frame to populate.
 * @param opcode   lumina_spi_cmd_t opcode byte.
 * @param payload  Pointer to LUMINA_SPI_PAYLOAD_LEN bytes of payload data.
 *                 Pass NULL to zero-fill the payload.
 */
static inline void lumina_spi_build_frame(lumina_spi_frame_t  *frame,
                                          uint8_t              opcode,
                                          const uint8_t        payload[LUMINA_SPI_PAYLOAD_LEN])
{
    frame->sof    = LUMINA_SPI_SOF;
    frame->opcode = opcode;
    if (payload != (const uint8_t *)0)
    {
        memcpy(frame->payload, payload, LUMINA_SPI_PAYLOAD_LEN);
    }
    else
    {
        memset(frame->payload, 0x00U, LUMINA_SPI_PAYLOAD_LEN);
    }
    frame->crc8   = lumina_spi_compute_crc((const uint8_t *)frame);
    frame->eof    = LUMINA_SPI_EOF;
}

#ifdef __cplusplus
}
#endif

#endif /* LUMINA_SAFETY_PROTOCOL_H */
