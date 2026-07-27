/**
 * @file    lumina_safety_protocol.h
 * @brief   LCU-100 ↔ Safety MCU SPI frame protocol definitions
 *
 * The LCU-100 (STM32H743) acts as SPI master; the Safety MCU (STM32G071)
 * acts as SPI slave.  Every 10 ms the LCU sends a fixed-length command
 * frame and clocks out an equally-sized response frame simultaneously
 * (full-duplex).
 *
 * Frame structure (8 bytes each direction):
 *   Byte 0   : Opcode
 *   Byte 1   : Payload byte 0  (request: phase ID; response: safety state)
 *   Byte 2   : Payload byte 1  (request: demand flags; response: permissive)
 *   Byte 3   : Payload byte 2  (request: reserved; response: fault flags high)
 *   Byte 4   : Payload byte 3  (request: reserved; response: fault flags low)
 *   Byte 5   : Sequence counter (LCU increments, safety MCU echoes)
 *   Byte 6   : CRC-8 of bytes 0-5 (polynomial 0x07, init 0x00)
 *   Byte 7   : Frame terminator 0xAA
 */

#ifndef __LUMINA_SAFETY_PROTOCOL_H
#define __LUMINA_SAFETY_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* --------------------------------------------------------------------------
 * Frame geometry
 * -------------------------------------------------------------------------- */
#define SAFETY_FRAME_LEN            8U
#define SAFETY_FRAME_TERMINATOR     0xAAU
#define SAFETY_SEQ_IDX              5U
#define SAFETY_CRC_IDX              6U
#define SAFETY_TERM_IDX             7U

/* --------------------------------------------------------------------------
 * Opcodes (LCU → Safety MCU)
 * -------------------------------------------------------------------------- */
typedef enum {
    SAFETY_OP_HEARTBEAT         = 0x01, /**< Periodic keepalive, no phase change    */
    SAFETY_OP_REQUEST_PHASE     = 0x02, /**< Request permissive for new phase        */
    SAFETY_OP_INHIBIT           = 0x03, /**< Assert inhibit line, all outputs dark   */
    SAFETY_OP_RELEASE_INHIBIT   = 0x04, /**< Release inhibit (safety MCU may deny)   */
    SAFETY_OP_SELF_TEST         = 0x05, /**< Trigger safety MCU self-test sequence   */
    SAFETY_OP_RESET             = 0xFEU,/**< Request controlled safety MCU reset     */
} safety_opcode_t;

/* --------------------------------------------------------------------------
 * Safety MCU state (response byte 1)
 * -------------------------------------------------------------------------- */
typedef enum {
    SAFETY_STATE_INIT           = 0x00, /**< Safety MCU still initialising          */
    SAFETY_STATE_READY          = 0x01, /**< Healthy, accepting commands             */
    SAFETY_STATE_INHIBIT_ACTIVE = 0x02, /**< Inhibit line asserted                  */
    SAFETY_STATE_SELF_TEST      = 0x03, /**< Self-test in progress                  */
    SAFETY_STATE_FAULT          = 0x04, /**< Internal fault detected                */
    SAFETY_STATE_FAULT_LOCKOUT  = 0x05, /**< Locked out, requires physical reset     */
} safety_mcu_state_t;

/* --------------------------------------------------------------------------
 * Permissive result (response byte 2)
 * -------------------------------------------------------------------------- */
typedef enum {
    PERMISSIVE_NONE             = 0x00, /**< No permissive decision pending         */
    PERMISSIVE_GRANTED          = 0x01, /**< Phase transition approved               */
    PERMISSIVE_DENIED_FAULT     = 0x02, /**< Denied: safety fault active             */
    PERMISSIVE_DENIED_TIMING    = 0x03, /**< Denied: intergreen timer not expired    */
    PERMISSIVE_DENIED_CONFLICT  = 0x04, /**< Denied: conflicting phase request       */
    PERMISSIVE_DENIED_INHIBIT   = 0x05, /**< Denied: inhibit line is asserted        */
    PERMISSIVE_PENDING          = 0x06, /**< Request received, evaluating            */
} permissive_result_t;

/* --------------------------------------------------------------------------
 * Safety MCU fault flags (response bytes 3-4, 16 bits)
 * -------------------------------------------------------------------------- */
#define SAFETY_FAULT_LAMP_MONITOR       ( 1U << 0 )
#define SAFETY_FAULT_OUTPUT_SHORTED     ( 1U << 1 )
#define SAFETY_FAULT_CONFLICT_DETECT    ( 1U << 2 )
#define SAFETY_FAULT_WATCHDOG           ( 1U << 3 )
#define SAFETY_FAULT_SUPPLY_RAIL        ( 1U << 4 )
#define SAFETY_FAULT_INTERNAL_FLASH     ( 1U << 5 )
#define SAFETY_FAULT_COMMS_LOSS         ( 1U << 6 )
#define SAFETY_FAULT_SELF_TEST_FAIL     ( 1U << 7 )

/* --------------------------------------------------------------------------
 * Composed frame types for convenience
 * -------------------------------------------------------------------------- */
typedef struct __attribute__((packed)) {
    uint8_t opcode;
    uint8_t requested_phase_id;
    uint8_t demand_flags;
    uint8_t reserved[2];
    uint8_t sequence;
    uint8_t crc8;
    uint8_t terminator;
} safety_cmd_frame_t;

typedef struct __attribute__((packed)) {
    uint8_t opcode_echo;
    uint8_t safety_state;       /**< safety_mcu_state_t   */
    uint8_t permissive_result;  /**< permissive_result_t  */
    uint8_t fault_flags_high;
    uint8_t fault_flags_low;
    uint8_t sequence_echo;
    uint8_t crc8;
    uint8_t terminator;
} safety_rsp_frame_t;

#ifdef __cplusplus
}
#endif

#endif /* __LUMINA_SAFETY_PROTOCOL_H */
