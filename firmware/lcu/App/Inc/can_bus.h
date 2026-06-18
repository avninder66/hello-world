/**
 * @file    can_bus.h
 * @brief   CAN FD bus driver header for the STM32H743 FDCAN1 peripheral (Lumina LCU-100).
 *
 * Provides a thin abstraction over the STM32H7 HAL FDCAN driver.  The LCU-100
 * uses a single FDCAN1 port operating at:
 *   - Nominal (arbitration) bit rate: 1 Mbit/s
 *   - Data phase bit rate:            4 Mbit/s
 *
 * Timing values are pre-computed for the 64 MHz FDCAN kernel clock derived from
 * the STM32H743 PLL1_Q output.
 *
 * Frame format: CAN FD, extended-DLC up to 64 data bytes, standard 11-bit IDs.
 *
 * Error handling:
 *   - Bus-off recovery is initiated automatically by can_bus_get_error_state()
 *     when a BUS_OFF condition is detected.
 *   - Rx overflows are counted and exposed via diagnostics.
 *
 * Copyright (c) Lumina Traffic Systems Ltd.  All rights reserved.
 * SPDX-License-Identifier: Proprietary
 */

#ifndef CAN_BUS_H
#define CAN_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32h7xx_hal.h"
#include "lumina_can_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Timing constants for FDCAN at 64 MHz kernel clock
 *
 * Nominal (1 Mbit/s):
 *   TQ = 1 / (64 MHz / Prescaler)
 *   Bit time = TQ × (1 + NominalTimeSeg1 + NominalTimeSeg2)
 *   With Prescaler=1: TQ = 15.625 ns
 *   NominalTimeSeg1=47, NominalTimeSeg2=16 → bit time = (1+47+16) × 15.625 = 1000 ns = 1 Mbit/s
 *   NominalSyncJumpWidth must be ≤ NominalTimeSeg2 = 16 (set to 16)
 *   Sample point = (1 + 47) / 64 = 75.0%
 *
 * Data phase (4 Mbit/s):
 *   Same Prescaler=1, so TQ = 15.625 ns
 *   DataTimeSeg1=11, DataTimeSeg2=4 → bit time = (1+11+4) × 15.625 = 250 ns = 4 Mbit/s
 *   DataSyncJumpWidth ≤ DataTimeSeg2 = 4 (set to 4)
 *   Sample point = (1 + 11) / 16 = 75.0%
 * ========================================================================= */

#define CAN_NOMINAL_PRESCALER       1U
#define CAN_NOMINAL_TIME_SEG1       47U
#define CAN_NOMINAL_TIME_SEG2       16U
#define CAN_NOMINAL_SYNC_JUMP_WIDTH 16U

#define CAN_DATA_PRESCALER          1U
#define CAN_DATA_TIME_SEG1          11U
#define CAN_DATA_TIME_SEG2          4U
#define CAN_DATA_SYNC_JUMP_WIDTH    4U

/* =========================================================================
 * Rx/Tx queue depths
 * ========================================================================= */

/** Depth of the software Rx ring buffer (must be a power of two). */
#define CAN_RX_QUEUE_DEPTH          16U

/** Depth of the software Tx queue (must be a power of two). */
#define CAN_TX_QUEUE_DEPTH          8U

/* =========================================================================
 * CAN message structure
 * ========================================================================= */

/** Maximum CAN FD payload size in bytes. */
#define CAN_FD_MAX_DLC              64U

/**
 * @brief Unified CAN FD message descriptor.
 *
 * Used for both Rx and Tx paths.  Timestamps are derived from the FDCAN
 * timestamp counter, which is incremented at the FDCAN peripheral clock rate
 * and wraps at 0xFFFF.  The driver extends this to 32 bits using a software
 * epoch counter in the ISR.
 */
typedef struct
{
    uint32_t id;                    /**< 11-bit standard CAN ID                     */
    uint8_t  dlc;                   /**< Data length (0–64 bytes for CAN FD)        */
    uint8_t  data[CAN_FD_MAX_DLC];  /**< Payload bytes                              */
    uint32_t timestamp;             /**< Free-running 32-bit capture timestamp (µs) */
    bool     is_fd_frame;           /**< true if received/sent as CAN FD frame      */
    bool     brs;                   /**< true if bit-rate switching was used         */
} can_message_t;

/* =========================================================================
 * Rx callback typedef
 * ========================================================================= */

/**
 * @brief Application-registered Rx callback prototype.
 *
 * Called from the CAN Rx processing task (not from ISR context) with a pointer
 * to the received message.  The message is valid only for the duration of the
 * callback — copy it if the data needs to outlive the call.
 *
 * @param msg   Pointer to the received CAN FD message.
 */
typedef void (*can_rx_callback_t)(const can_message_t *msg);

/* =========================================================================
 * Error state enumeration
 * ========================================================================= */

/**
 * @brief CAN bus error state levels.
 *
 * These map directly to the FDCAN PSR (Protocol Status Register) error states.
 * The driver promotes to higher error levels based on TEC/REC counters reported
 * by the FDCAN peripheral.
 */
typedef enum
{
    CAN_ERROR_NONE      = 0U,   /**< TEC and REC both below warning threshold (96)  */
    CAN_ERROR_WARNING   = 1U,   /**< At least one counter ≥ 96 (warning limit)      */
    CAN_ERROR_PASSIVE   = 2U,   /**< At least one counter ≥ 128 (error passive)     */
    CAN_ERROR_BUS_OFF   = 3U,   /**< TEC ≥ 256; bus-off state, Tx halted            */
} can_error_state_t;

/* =========================================================================
 * Driver statistics (exposed for telemetry)
 * ========================================================================= */

/**
 * @brief Runtime CAN bus statistics.
 *
 * Updated by the driver; read by the telemetry task via can_bus_get_stats().
 */
typedef struct
{
    uint32_t tx_frames;             /**< Total frames successfully transmitted       */
    uint32_t rx_frames;             /**< Total frames successfully received          */
    uint32_t tx_errors;             /**< Frames dropped due to Tx FIFO full         */
    uint32_t rx_overflows;          /**< Rx queue overflow events                   */
    uint32_t bus_off_events;        /**< Number of bus-off recovery cycles          */
    uint32_t crc_errors;            /**< CAN FD CRC error count (from PSR register) */
    uint32_t form_errors;           /**< Form error count                            */
    uint32_t stuff_errors;          /**< Bit-stuffing error count                   */
} can_bus_stats_t;

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * @brief Initialise the FDCAN1 peripheral and associated resources.
 *
 * Configures FDCAN1 at 1 Mbit/s nominal / 4 Mbit/s data, sets up acceptance
 * filters to receive Lumina protocol messages, and enables Rx FIFO0 interrupts.
 *
 * Must be called once after HAL_Init() and SystemClock_Config().  The task
 * scheduler must be running before calling this function if using FreeRTOS
 * queues internally.
 *
 * @return  true on success; false if HAL initialisation failed.
 */
bool can_bus_init(void);

/**
 * @brief Transmit a CAN FD message.
 *
 * Adds the message to the FDCAN1 Tx FIFO.  If the hardware Tx FIFO is full,
 * the message is queued in the software Tx queue.  If the software queue is
 * also full, the oldest entry is dropped and tx_errors is incremented.
 *
 * This function is safe to call from any task context.  It is NOT safe to
 * call from ISR context (uses FreeRTOS queue API with timeout = 0).
 *
 * @param msg   Pointer to the message to transmit.  The caller retains ownership.
 * @return      true if the message was accepted (queued or sent); false if dropped.
 */
bool can_bus_transmit(const can_message_t *msg);

/**
 * @brief Register an application callback for received messages.
 *
 * Up to CAN_MAX_RX_CALLBACKS handlers may be registered.  Each received frame
 * is dispatched to the callback whose registered ID matches the frame's CAN ID.
 * Use id = 0xFFFFFFFF as a wildcard to receive all frames.
 *
 * Callbacks are invoked from the can_rx_task context (not ISR context).
 *
 * @param id        CAN ID to filter on, or 0xFFFFFFFF for wildcard.
 * @param callback  Function pointer to invoke on reception of a matching frame.
 * @return          true if the callback was registered; false if the table is full.
 */
bool can_bus_register_rx_callback(uint32_t id, can_rx_callback_t callback);

/**
 * @brief Return the current depth of the software Tx queue.
 *
 * Useful for monitoring Tx back-pressure.  A persistently non-zero depth
 * indicates the bus is unable to keep up with the application transmit rate.
 *
 * @return  Number of messages pending in the software Tx queue (0–CAN_TX_QUEUE_DEPTH).
 */
uint32_t can_bus_get_tx_queue_depth(void);

/**
 * @brief Return the current CAN bus error state.
 *
 * Queries the FDCAN PSR register.  If BUS_OFF is detected, a recovery sequence
 * is initiated automatically (FDCAN bus-off recovery requires 128 × 11 recessive
 * bits; the HAL handles this via HAL_FDCAN_RecoverFromBusOff()).
 *
 * @return  can_error_state_t current error level.
 */
can_error_state_t can_bus_get_error_state(void);

/**
 * @brief Copy the current driver statistics into a caller-supplied struct.
 *
 * @param stats_out  Pointer to a can_bus_stats_t to fill.
 */
void can_bus_get_stats(can_bus_stats_t *stats_out);

/**
 * @brief FreeRTOS task entry point for CAN Rx processing.
 *
 * Dequeues messages from the ISR-populated Rx ring buffer and dispatches them
 * to registered callbacks.  Should be created at a priority above the telemetry
 * and phase-command tasks but below the safety supervisor SPI task.
 *
 * Stack requirement: ~512 words.
 *
 * @param argument  Unused (required by FreeRTOS task prototype).
 */
void can_rx_task(void *argument);

/* =========================================================================
 * Acceptance filter IDs
 *
 * The FDCAN1 hardware filter bank is configured to pass:
 *   - All heartbeats from LSO, LPB, LPI, and the safety supervisor.
 *   - LSO output status frames.
 *   - LPB battery status frames.
 *   - LPI pedestrian event frames.
 *   - Safety supervisor fault reports.
 *   - Broadcast ID 0x7FF (used for emergency stop by any node).
 *
 * LCU's own transmitted IDs are not filtered for Rx (no loopback in normal mode).
 * ========================================================================= */

#define CAN_FILTER_ID_LSO_HEARTBEAT     LUMINA_ID_LSO_HEARTBEAT
#define CAN_FILTER_ID_LPB_HEARTBEAT     LUMINA_ID_LPB_HEARTBEAT
#define CAN_FILTER_ID_LPI_HEARTBEAT     LUMINA_ID_LPI_HEARTBEAT
#define CAN_FILTER_ID_SAFETY_HEARTBEAT  LUMINA_ID_SAFETY_HEARTBEAT
#define CAN_FILTER_ID_LSO_OUTPUT_STATUS LUMINA_ID_LSO_OUTPUT_STATUS
#define CAN_FILTER_ID_LPB_BATT_STATUS   LUMINA_ID_LPB_BATTERY_STATUS
#define CAN_FILTER_ID_LPI_PED_EVENT     LUMINA_ID_LPI_PEDESTRIAN_EVENT
#define CAN_FILTER_ID_SAFETY_FAULT      LUMINA_ID_SAFETY_FAULT_REPORT
#define CAN_FILTER_ID_LSO_FAULT         LUMINA_ID_LSO_FAULT_REPORT
#define CAN_FILTER_ID_LPB_FAULT         LUMINA_ID_LPB_FAULT_REPORT
#define CAN_FILTER_ID_BROADCAST         0x7FFU

/** Maximum number of Rx callbacks that can be registered simultaneously. */
#define CAN_MAX_RX_CALLBACKS            12U

#ifdef __cplusplus
}
#endif

#endif /* CAN_BUS_H */
