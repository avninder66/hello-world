/**
 * @file    lumina_can_protocol.h
 * @brief   CAN FD message protocol definitions for the Lumina LCU-100 system.
 *
 * Defines CAN base IDs, message type sub-IDs, packed payload structures,
 * and acceptance-filter helpers for all boards in the Lumina 4-board modular
 * temporary traffic signal controller.
 *
 * Board CAN base IDs:
 *   LCU-100 main controller  0x010
 *   LSO-100 signal output    0x020
 *   LPB-100 power/battery    0x030
 *   LPI-100 pedestrian I/F   0x040
 *   Safety supervisor MCU    0x050
 *
 * All frames use 11-bit standard identifiers composed as:
 *   CAN_ID = (BOARD_BASE_ID << 4) | MSG_TYPE_NIBBLE
 *
 * CAN FD bit rates: arbitration 500 kbit/s, data 2 Mbit/s.
 *
 * Copyright (c) Lumina Traffic Systems Ltd.  All rights reserved.
 * SPDX-License-Identifier: Proprietary
 */

#ifndef LUMINA_CAN_PROTOCOL_H
#define LUMINA_CAN_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Protocol versioning
 * ========================================================================= */

#define LUMINA_CAN_PROTOCOL_VERSION_MAJOR   1U
#define LUMINA_CAN_PROTOCOL_VERSION_MINOR   4U
#define LUMINA_CAN_PROTOCOL_VERSION_PATCH   0U

/** Packed 16-bit version word: [15:8] major, [7:4] minor, [3:0] patch. */
#define LUMINA_CAN_PROTOCOL_VERSION \
    ((uint16_t)(((LUMINA_CAN_PROTOCOL_VERSION_MAJOR) << 8U) | \
                ((LUMINA_CAN_PROTOCOL_VERSION_MINOR) << 4U) | \
                 (LUMINA_CAN_PROTOCOL_VERSION_PATCH)))

/* =========================================================================
 * Cycle / timeout constants  (all in milliseconds)
 * ========================================================================= */

/** Heartbeat transmission interval for all nodes. */
#define LUMINA_HEARTBEAT_CYCLE_MS           100U

/** Maximum tolerated heartbeat absence before link-loss fault. */
#define LUMINA_HEARTBEAT_TIMEOUT_MS         350U

/** Phase command transmission interval. */
#define LUMINA_PHASE_CMD_CYCLE_MS           200U

/** Output status report interval from LSO. */
#define LUMINA_OUTPUT_STATUS_CYCLE_MS       200U

/** Battery status report interval from LPB. */
#define LUMINA_BATTERY_STATUS_CYCLE_MS      1000U

/** Telemetry burst interval. */
#define LUMINA_TELEMETRY_CYCLE_MS           500U

/* =========================================================================
 * Board base CAN IDs
 * ========================================================================= */

#define LUMINA_CAN_BASE_LCU                 0x010U  /**< Main controller          */
#define LUMINA_CAN_BASE_LSO                 0x020U  /**< Signal output board      */
#define LUMINA_CAN_BASE_LPB                 0x030U  /**< Power / battery board    */
#define LUMINA_CAN_BASE_LPI                 0x040U  /**< Pedestrian interface     */
#define LUMINA_CAN_BASE_SAFETY              0x050U  /**< Safety supervisor MCU    */

/* =========================================================================
 * Message type sub-IDs (4-bit nibble, appended to base ID)
 * ========================================================================= */

#define LUMINA_MSG_HEARTBEAT                0x0U
#define LUMINA_MSG_PHASE_COMMAND            0x1U
#define LUMINA_MSG_OUTPUT_STATUS            0x2U
#define LUMINA_MSG_FAULT_REPORT             0x3U
#define LUMINA_MSG_BATTERY_STATUS           0x4U
#define LUMINA_MSG_PEDESTRIAN_EVENT         0x5U
#define LUMINA_MSG_INHIBIT_COMMAND          0x6U
#define LUMINA_MSG_TELEMETRY                0x7U

/* =========================================================================
 * Composed 11-bit CAN IDs
 * Helper macro: LUMINA_MAKE_CAN_ID(base, type)
 * ========================================================================= */

#define LUMINA_MAKE_CAN_ID(base, type)      (((uint32_t)(base) << 4U) | ((uint32_t)(type) & 0x0FU))

/* --- LCU transmit IDs --- */
#define LUMINA_ID_LCU_HEARTBEAT             LUMINA_MAKE_CAN_ID(LUMINA_CAN_BASE_LCU, LUMINA_MSG_HEARTBEAT)
#define LUMINA_ID_LCU_PHASE_COMMAND         LUMINA_MAKE_CAN_ID(LUMINA_CAN_BASE_LCU, LUMINA_MSG_PHASE_COMMAND)
#define LUMINA_ID_LCU_FAULT_REPORT          LUMINA_MAKE_CAN_ID(LUMINA_CAN_BASE_LCU, LUMINA_MSG_FAULT_REPORT)
#define LUMINA_ID_LCU_INHIBIT_COMMAND       LUMINA_MAKE_CAN_ID(LUMINA_CAN_BASE_LCU, LUMINA_MSG_INHIBIT_COMMAND)
#define LUMINA_ID_LCU_TELEMETRY             LUMINA_MAKE_CAN_ID(LUMINA_CAN_BASE_LCU, LUMINA_MSG_TELEMETRY)

/* --- LSO transmit IDs --- */
#define LUMINA_ID_LSO_HEARTBEAT             LUMINA_MAKE_CAN_ID(LUMINA_CAN_BASE_LSO, LUMINA_MSG_HEARTBEAT)
#define LUMINA_ID_LSO_OUTPUT_STATUS         LUMINA_MAKE_CAN_ID(LUMINA_CAN_BASE_LSO, LUMINA_MSG_OUTPUT_STATUS)
#define LUMINA_ID_LSO_FAULT_REPORT          LUMINA_MAKE_CAN_ID(LUMINA_CAN_BASE_LSO, LUMINA_MSG_FAULT_REPORT)
#define LUMINA_ID_LSO_TELEMETRY             LUMINA_MAKE_CAN_ID(LUMINA_CAN_BASE_LSO, LUMINA_MSG_TELEMETRY)

/* --- LPB transmit IDs --- */
#define LUMINA_ID_LPB_HEARTBEAT             LUMINA_MAKE_CAN_ID(LUMINA_CAN_BASE_LPB, LUMINA_MSG_HEARTBEAT)
#define LUMINA_ID_LPB_BATTERY_STATUS        LUMINA_MAKE_CAN_ID(LUMINA_CAN_BASE_LPB, LUMINA_MSG_BATTERY_STATUS)
#define LUMINA_ID_LPB_FAULT_REPORT          LUMINA_MAKE_CAN_ID(LUMINA_CAN_BASE_LPB, LUMINA_MSG_FAULT_REPORT)
#define LUMINA_ID_LPB_TELEMETRY             LUMINA_MAKE_CAN_ID(LUMINA_CAN_BASE_LPB, LUMINA_MSG_TELEMETRY)

/* --- LPI transmit IDs --- */
#define LUMINA_ID_LPI_HEARTBEAT             LUMINA_MAKE_CAN_ID(LUMINA_CAN_BASE_LPI, LUMINA_MSG_HEARTBEAT)
#define LUMINA_ID_LPI_PEDESTRIAN_EVENT      LUMINA_MAKE_CAN_ID(LUMINA_CAN_BASE_LPI, LUMINA_MSG_PEDESTRIAN_EVENT)
#define LUMINA_ID_LPI_FAULT_REPORT          LUMINA_MAKE_CAN_ID(LUMINA_CAN_BASE_LPI, LUMINA_MSG_FAULT_REPORT)

/* --- Safety supervisor transmit IDs --- */
#define LUMINA_ID_SAFETY_HEARTBEAT          LUMINA_MAKE_CAN_ID(LUMINA_CAN_BASE_SAFETY, LUMINA_MSG_HEARTBEAT)
#define LUMINA_ID_SAFETY_FAULT_REPORT       LUMINA_MAKE_CAN_ID(LUMINA_CAN_BASE_SAFETY, LUMINA_MSG_FAULT_REPORT)
#define LUMINA_ID_SAFETY_OUTPUT_STATUS      LUMINA_MAKE_CAN_ID(LUMINA_CAN_BASE_SAFETY, LUMINA_MSG_OUTPUT_STATUS)

/* =========================================================================
 * CAN acceptance filter helpers
 *
 * STM32H7 FDCAN uses id/mask pairs in 32-bit format.
 * Filter mask 0x7F0 matches any message-type nibble from a given board.
 * Filter mask 0x7FF matches exactly one CAN ID.
 * ========================================================================= */

#define LUMINA_FILTER_MASK_BOARD            0x7F0U  /**< Accept all msgs from one board  */
#define LUMINA_FILTER_MASK_EXACT            0x7FFU  /**< Accept one specific CAN ID       */

/** Accept all Lumina protocol messages (board IDs 0x010–0x05F). */
#define LUMINA_FILTER_ALL_BOARDS_ID         0x010U
#define LUMINA_FILTER_ALL_BOARDS_MASK       0x700U

/* =========================================================================
 * Payload constants
 * ========================================================================= */

/** Maximum number of lamp output channels on LSO-100. */
#define LUMINA_LSO_CHANNEL_COUNT            12U

/** Maximum CAN FD data length (bytes) used by this protocol. */
#define LUMINA_CAN_MAX_DLC                  64U

/* =========================================================================
 * Node state enumeration (used in heartbeat frames)
 * ========================================================================= */

typedef enum
{
    LUMINA_NODE_STATE_BOOTING       = 0x00U, /**< Initialisation in progress     */
    LUMINA_NODE_STATE_OPERATIONAL   = 0x01U, /**< Normal running                  */
    LUMINA_NODE_STATE_WARNING       = 0x02U, /**< Active warning, still running   */
    LUMINA_NODE_STATE_FAULT         = 0x03U, /**< Non-safety fault, inhibited     */
    LUMINA_NODE_STATE_SAFE_LOCKOUT  = 0x04U, /**< Safety fault, outputs inhibited */
    LUMINA_NODE_STATE_SELF_TEST     = 0x05U, /**< Self-test sequence active       */
    LUMINA_NODE_STATE_SHUTDOWN      = 0x06U, /**< Orderly shutdown in progress    */
} lumina_node_state_t;

/* =========================================================================
 * HEARTBEAT payload  (8 bytes, sent by every node at 100 ms)
 * ========================================================================= */

#pragma pack(push, 1)

typedef struct
{
    uint8_t     node_id;            /**< Transmitting board ID (LUMINA_CAN_BASE_xxx) */
    uint8_t     state;              /**< lumina_node_state_t                         */
    uint16_t    proto_version;      /**< LUMINA_CAN_PROTOCOL_VERSION                 */
    uint16_t    uptime_s;           /**< Seconds since last power-on reset           */
    uint16_t    active_fault_code;  /**< Most-severe active fault, or 0x0000         */
} lumina_heartbeat_t;

/* =========================================================================
 * PHASE_COMMAND payload  (12 bytes, LCU → LSO at 200 ms)
 * ========================================================================= */

typedef struct
{
    uint8_t     sequence;           /**< Rolling 8-bit command sequence counter      */
    uint8_t     phase_id;           /**< Target phase index (0-based)                */
    uint8_t     operating_mode;     /**< lumina_operating_mode_t                     */
    uint8_t     flags;              /**< Bit 0: demand_active, Bit 1: ped_green_req  */
    uint16_t    phase_duration_ms;  /**< Remaining active duration (0 = indefinite)  */
    uint16_t    reserved;           /**< Must be 0x0000                              */

    /**
     * Packed channel output bitmap, one nibble per channel (channels 0-11).
     * Each nibble encodes lumina_lamp_state_t for that channel:
     *   0x0 = OFF, 0x1 = ON, 0x2 = FLASH (500 ms), 0x3 = FLASH_FAST (250 ms)
     * Bytes [8..11]: channels 0-7 in byte[8..9], channels 8-11 in byte[10..11].
     * Arranged as: byte[n] = (ch[2n+1] << 4) | ch[2n]
     */
    uint8_t     channel_states[4];
} lumina_phase_command_t;

/* =========================================================================
 * OUTPUT_STATUS payload  (16 bytes, LSO → all at 200 ms)
 * ========================================================================= */

typedef struct
{
    uint8_t     sequence;               /**< Echo of last received phase_command seq  */
    uint8_t     active_phase_id;        /**< Currently executing phase                */
    uint8_t     operating_mode;         /**< Current operating mode                   */

    /**
     * Per-channel confirmed state bitmap.
     * Bits [11:0] of confirmed_on: channel is driven ON and load current OK.
     * Bits [11:0] of confirmed_fault: channel has a load fault (open/short/thermal).
     */
    uint16_t    confirmed_on;           /**< Channel ON confirmation bitmask          */
    uint16_t    confirmed_fault;        /**< Channel fault bitmask                    */

    /**
     * PROFET diagnostic current feedback, one byte per channel (0-11).
     * Value = raw ADC count / 4 (range 0–63 mA equivalent, 0xFF = overcurrent trip).
     */
    uint8_t     channel_current[12];    /**< Load current diagnostic per channel      */

    uint8_t     board_temp_c;           /**< PCB temperature in degrees C (offset 40) */
    uint8_t     reserved;               /**< Must be 0x00                             */
} lumina_output_status_t;

/* =========================================================================
 * FAULT_REPORT payload  (8 bytes, any node → all)
 * ========================================================================= */

typedef struct
{
    uint8_t     source_node_id;     /**< LUMINA_CAN_BASE_xxx of reporting node        */
    uint8_t     severity;           /**< lumina_fault_severity_t                     */
    uint16_t    fault_code;         /**< fault_codes.h code (FAULT_xxx)              */
    uint32_t    fault_data;         /**< Context-specific diagnostic data            */
} lumina_fault_report_t;

/* =========================================================================
 * BATTERY_STATUS payload  (12 bytes, LPB → all at 1000 ms)
 * ========================================================================= */

typedef struct
{
    uint16_t    bus_voltage_mv;     /**< DC bus voltage in millivolts                */
    uint16_t    battery_voltage_mv; /**< Battery float voltage in millivolts         */
    int16_t     battery_current_ma; /**< Battery current: +ve = charging             */
    uint8_t     soc_percent;        /**< State of charge, 0–100 %                    */
    uint8_t     charger_state;      /**< lumina_charger_state_t                      */
    uint8_t     mains_present  : 1; /**< 1 = mains AC input present                  */
    uint8_t     battery_present: 1; /**< 1 = battery detected                        */
    uint8_t     low_battery    : 1; /**< 1 = SoC below low-battery threshold         */
    uint8_t     critical_batt  : 1; /**< 1 = SoC below critical threshold            */
    uint8_t     over_voltage   : 1; /**< 1 = bus over-voltage trip                   */
    uint8_t     under_voltage  : 1; /**< 1 = bus under-voltage trip                  */
    uint8_t     overtemp       : 1; /**< 1 = battery over-temperature                */
    uint8_t     reserved_bits  : 1;
    uint8_t     board_temp_c;       /**< LPB PCB temperature (offset 40)             */
    uint8_t     reserved;           /**< Must be 0x00                                */
} lumina_battery_status_t;

/* =========================================================================
 * PEDESTRIAN_EVENT payload  (4 bytes, LPI → LCU)
 * ========================================================================= */

typedef struct
{
    uint8_t     approach_id;        /**< Approach index (0-based) where push occurred */
    uint8_t     event_type;         /**< lumina_ped_event_t                           */
    uint8_t     push_count;         /**< Number of button presses since last clear    */
    uint8_t     flags;              /**< Bit 0: audible_tone_active, Bit 1: tactile_active */
} lumina_pedestrian_event_t;

/* =========================================================================
 * INHIBIT_COMMAND payload  (4 bytes, LCU → LSO / Safety)
 * ========================================================================= */

typedef struct
{
    uint8_t     sequence;           /**< Rolling sequence counter                    */
    uint8_t     inhibit_reason;     /**< lumina_inhibit_reason_t                     */
    uint16_t    channel_mask;       /**< Bit per channel: 1 = inhibit that channel   */
} lumina_inhibit_command_t;

/* =========================================================================
 * TELEMETRY payload  (48 bytes, LCU → all at 500 ms)
 *
 * Provides controller-side operational metrics for logging and remote
 * monitoring over RS485/GPRS uplink.
 * ========================================================================= */

typedef struct
{
    uint32_t    timestamp_ms;           /**< Controller millisecond tick              */
    uint32_t    cycle_count;            /**< Phase cycle counter since power-on       */
    uint16_t    current_phase_time_ms;  /**< Time elapsed in current phase            */
    uint16_t    last_phase_duration_ms; /**< Actual duration of previous phase        */
    uint8_t     current_phase_id;       /**< Active phase index                       */
    uint8_t     operating_mode;         /**< lumina_operating_mode_t                  */
    uint8_t     active_approaches;      /**< Bitmask of approaches with demand        */
    uint8_t     ped_demand_mask;        /**< Bitmask of approaches with ped demand    */
    uint16_t    fault_count_total;      /**< Cumulative fault events this session     */
    uint16_t    demand_count_total;     /**< Cumulative detector actuations           */
    uint8_t     lcu_cpu_temp_c;         /**< STM32H743 die temperature (offset 40)    */
    uint8_t     lso_board_temp_c;       /**< LSO PCB temperature (offset 40)          */
    uint8_t     lpb_board_temp_c;       /**< LPB PCB temperature (offset 40)          */
    uint8_t     safety_state;           /**< lumina_safety_state_t                    */
    uint16_t    bus_voltage_mv;         /**< DC bus snapshot from LPB                 */
    uint8_t     soc_percent;            /**< Battery SoC snapshot                     */
    uint8_t     comms_error_flags;      /**< Bit per board: 1 = link loss detected    */
    uint8_t     lso_fault_mask_lo;      /**< LSO channel fault bits [7:0]             */
    uint8_t     lso_fault_mask_hi;      /**< LSO channel fault bits [11:8]            */
    uint8_t     reserved[14];           /**< Pad to 48 bytes, must be 0x00            */
} lumina_telemetry_t;

#pragma pack(pop)

/* =========================================================================
 * Supporting enumerations (used in payload fields above)
 * ========================================================================= */

/** Lamp channel drive state, encoded in phase_command channel_states nibbles. */
typedef enum
{
    LUMINA_LAMP_OFF         = 0x0U, /**< Channel de-energised                        */
    LUMINA_LAMP_ON          = 0x1U, /**< Channel energised steady                    */
    LUMINA_LAMP_FLASH       = 0x2U, /**< Flash at 500 ms half-period                 */
    LUMINA_LAMP_FLASH_FAST  = 0x3U, /**< Flash at 250 ms half-period                 */
} lumina_lamp_state_t;

/** Charger state codes reported by LPB. */
typedef enum
{
    LUMINA_CHARGER_OFF          = 0x00U,
    LUMINA_CHARGER_BULK         = 0x01U,
    LUMINA_CHARGER_ABSORPTION   = 0x02U,
    LUMINA_CHARGER_FLOAT        = 0x03U,
    LUMINA_CHARGER_FAULT        = 0x04U,
    LUMINA_CHARGER_NO_BATTERY   = 0x05U,
} lumina_charger_state_t;

/** Pedestrian interface event types. */
typedef enum
{
    LUMINA_PED_EVENT_PUSH_SINGLE    = 0x01U, /**< Single button press                */
    LUMINA_PED_EVENT_PUSH_DOUBLE    = 0x02U, /**< Double press within 500 ms         */
    LUMINA_PED_EVENT_DEMAND_CLEARED = 0x03U, /**< Demand acknowledged by controller  */
    LUMINA_PED_EVENT_FAULT_PUSHBTN  = 0x04U, /**< Push button circuit fault          */
    LUMINA_PED_EVENT_FAULT_DISPLAY  = 0x05U, /**< Ped signal display fault           */
} lumina_ped_event_t;

/** Inhibit reason codes used in INHIBIT_COMMAND. */
typedef enum
{
    LUMINA_INHIBIT_NONE             = 0x00U,
    LUMINA_INHIBIT_FAULT            = 0x01U, /**< Non-safety fault inhibit            */
    LUMINA_INHIBIT_SAFETY           = 0x02U, /**< Safety fault lockout                */
    LUMINA_INHIBIT_MANUAL           = 0x03U, /**< Operator-commanded inhibit          */
    LUMINA_INHIBIT_COMMS_LOSS       = 0x04U, /**< CAN link loss to LSO                */
    LUMINA_INHIBIT_POWER_CRITICAL   = 0x05U, /**< Battery critical level              */
    LUMINA_INHIBIT_SELF_TEST        = 0x06U, /**< Self-test sequence in progress      */
} lumina_inhibit_reason_t;

/** Fault severity levels, mirrored from fault_codes.h for CAN payload use. */
typedef enum
{
    LUMINA_SEVERITY_INFO     = 0x00U,
    LUMINA_SEVERITY_WARNING  = 0x01U,
    LUMINA_SEVERITY_CRITICAL = 0x02U,
    LUMINA_SEVERITY_SAFETY   = 0x03U,
} lumina_fault_severity_t;

/* =========================================================================
 * Compile-time size assertions
 * ========================================================================= */

#ifndef __cplusplus
_Static_assert(sizeof(lumina_heartbeat_t)           ==  8U, "heartbeat size");
_Static_assert(sizeof(lumina_phase_command_t)       == 12U, "phase_command size");
_Static_assert(sizeof(lumina_output_status_t)       == 16U, "output_status size");
_Static_assert(sizeof(lumina_fault_report_t)        ==  8U, "fault_report size");
_Static_assert(sizeof(lumina_battery_status_t)      == 12U, "battery_status size");
_Static_assert(sizeof(lumina_pedestrian_event_t)    ==  4U, "pedestrian_event size");
_Static_assert(sizeof(lumina_inhibit_command_t)     ==  4U, "inhibit_command size");
_Static_assert(sizeof(lumina_telemetry_t)           == 48U, "telemetry size");
#endif

/* =========================================================================
 * Inline helpers
 * ========================================================================= */

/**
 * @brief Extract the board base ID from a received CAN ID.
 * @param can_id  11-bit CAN identifier.
 * @return        Board base ID (LUMINA_CAN_BASE_xxx).
 */
static inline uint32_t lumina_can_board_id(uint32_t can_id)
{
    return (can_id >> 4U) & 0x7FU;
}

/**
 * @brief Extract the message type nibble from a received CAN ID.
 * @param can_id  11-bit CAN identifier.
 * @return        Message type (LUMINA_MSG_xxx).
 */
static inline uint32_t lumina_can_msg_type(uint32_t can_id)
{
    return can_id & 0x0FU;
}

/**
 * @brief Encode a lamp state into the channel_states byte array.
 *
 * @param states    Pointer to the 4-byte channel_states array.
 * @param channel   Channel index 0–11.
 * @param state     lumina_lamp_state_t value.
 */
static inline void lumina_set_channel_state(uint8_t states[4],
                                             uint8_t channel,
                                             lumina_lamp_state_t state)
{
    if (channel >= LUMINA_LSO_CHANNEL_COUNT) { return; }
    uint8_t byte_idx = channel / 2U;
    if ((channel & 1U) == 0U)
    {
        states[byte_idx] = (uint8_t)((states[byte_idx] & 0xF0U) | ((uint8_t)state & 0x0FU));
    }
    else
    {
        states[byte_idx] = (uint8_t)((states[byte_idx] & 0x0FU) | (((uint8_t)state & 0x0FU) << 4U));
    }
}

/**
 * @brief Decode a lamp state from the channel_states byte array.
 *
 * @param states    Pointer to the 4-byte channel_states array.
 * @param channel   Channel index 0–11.
 * @return          lumina_lamp_state_t for that channel.
 */
static inline lumina_lamp_state_t lumina_get_channel_state(const uint8_t states[4],
                                                            uint8_t channel)
{
    if (channel >= LUMINA_LSO_CHANNEL_COUNT) { return LUMINA_LAMP_OFF; }
    uint8_t byte_idx = channel / 2U;
    uint8_t nibble = ((channel & 1U) == 0U)
                     ? (states[byte_idx] & 0x0FU)
                     : ((states[byte_idx] >> 4U) & 0x0FU);
    return (lumina_lamp_state_t)nibble;
}

#ifdef __cplusplus
}
#endif

#endif /* LUMINA_CAN_PROTOCOL_H */
