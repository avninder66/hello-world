/**
 * @file    can_bus.c
 * @brief   CAN FD bus driver implementation — STM32H743 FDCAN1 (Lumina LCU-100).
 *
 * Implements the can_bus.h API using the STM32H7 HAL FDCAN driver.
 *
 * Rx path:
 *   HAL_FDCAN_RxFifo0Callback (ISR) → g_rx_ring_buf (ring buffer, ISR-safe) →
 *   can_rx_task (FreeRTOS task) → registered callbacks
 *
 * Tx path:
 *   can_bus_transmit() → tries HAL_FDCAN_AddMessageToTxFifo() directly →
 *   on HW FIFO full: enqueue to g_tx_queue (FreeRTOS queue) →
 *   drained by can_tx_drain() called from can_rx_task main loop.
 *
 * Bus-off recovery:
 *   can_bus_get_error_state() detects BUS_OFF via PSR register and calls
 *   HAL_FDCAN_RecoverFromBusOff(), then re-enables Rx notifications.
 *
 * Copyright (c) Lumina Traffic Systems Ltd.  All rights reserved.
 * SPDX-License-Identifier: Proprietary
 */

#include "can_bus.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <string.h>
#include <stddef.h>

/* =========================================================================
 * Private constants
 * ========================================================================= */

/** Timeout (ms) for FreeRTOS queue operations in can_rx_task. */
#define CAN_RX_TASK_BLOCK_MS        10U

/** Mask for ring buffer index wrap (buffer size must be power of two). */
#define RX_RING_MASK    (CAN_RX_QUEUE_DEPTH - 1U)
#define TX_QUEUE_MASK   (CAN_TX_QUEUE_DEPTH  - 1U)

/* Verify power-of-two constraint at compile time. */
_Static_assert((CAN_RX_QUEUE_DEPTH & (CAN_RX_QUEUE_DEPTH - 1U)) == 0U,
               "CAN_RX_QUEUE_DEPTH must be a power of two");
_Static_assert((CAN_TX_QUEUE_DEPTH  & (CAN_TX_QUEUE_DEPTH  - 1U)) == 0U,
               "CAN_TX_QUEUE_DEPTH must be a power of two");

/* =========================================================================
 * FDCAN handle (global so HAL callbacks can reach it)
 * ========================================================================= */

FDCAN_HandleTypeDef hfdcan1;

/* =========================================================================
 * Rx ring buffer (ISR → task, lock-free single-producer single-consumer)
 * ========================================================================= */

static can_message_t g_rx_ring_buf[CAN_RX_QUEUE_DEPTH];
static volatile uint32_t g_rx_write_idx = 0U;  /* written by ISR  */
static volatile uint32_t g_rx_read_idx  = 0U;  /* read by Rx task */

/* =========================================================================
 * Tx software queue (task context → HAL Tx FIFO drain)
 * =========================================================================
 * Protected by a FreeRTOS binary semaphore (g_tx_mutex) since it can be
 * written by multiple tasks and drained by can_rx_task.
 */

static can_message_t g_tx_queue_buf[CAN_TX_QUEUE_DEPTH];
static uint32_t      g_tx_write_idx = 0U;
static uint32_t      g_tx_read_idx  = 0U;
static SemaphoreHandle_t g_tx_mutex = NULL;

/* =========================================================================
 * Callback registration table
 * ========================================================================= */

typedef struct
{
    uint32_t          id;
    can_rx_callback_t cb;
    bool              active;
} rx_callback_entry_t;

static rx_callback_entry_t g_rx_callbacks[CAN_MAX_RX_CALLBACKS];
static uint32_t            g_rx_callback_count = 0U;

/* Mutex protecting the callback table (registered from task contexts). */
static SemaphoreHandle_t g_cb_mutex = NULL;

/* =========================================================================
 * Driver statistics
 * ========================================================================= */

static can_bus_stats_t g_stats;

/* =========================================================================
 * Timestamp extension — 32-bit µs clock derived from the FDCAN 16-bit counter
 * ========================================================================= */

static volatile uint32_t g_ts_epoch = 0U;  /* Upper 16 bits, incremented on wrap */

/* =========================================================================
 * Forward declarations
 * ========================================================================= */

static void     can_filter_init(void);
static bool     can_hw_transmit(const can_message_t *msg);
static void     can_tx_drain(void);
static uint32_t can_timestamp_us(uint16_t hw_ts);
static uint8_t  dlc_to_bytes(uint32_t dlc_code);
static uint32_t bytes_to_dlc(uint8_t data_len);

/* =========================================================================
 * can_bus_init()
 * ========================================================================= */

bool can_bus_init(void)
{
    memset(&g_stats,        0, sizeof(g_stats));
    memset(g_rx_callbacks,  0, sizeof(g_rx_callbacks));
    g_rx_callback_count = 0U;

    /* Create synchronisation primitives. */
    g_tx_mutex = xSemaphoreCreateMutex();
    g_cb_mutex = xSemaphoreCreateMutex();
    if ((g_tx_mutex == NULL) || (g_cb_mutex == NULL))
    {
        return false;
    }

    /* ---- FDCAN1 peripheral configuration ---- */
    hfdcan1.Instance                  = FDCAN1;
    hfdcan1.Init.ClockDivider         = FDCAN_CLOCK_DIV1;
    hfdcan1.Init.FrameFormat          = FDCAN_FRAME_FD_BRS;
    hfdcan1.Init.Mode                 = FDCAN_MODE_NORMAL;
    hfdcan1.Init.AutoRetransmission   = ENABLE;
    hfdcan1.Init.TransmitPause        = ENABLE;
    hfdcan1.Init.ProtocolException    = ENABLE;

    /* Nominal 1 Mbit/s (64 MHz kernel clock) */
    hfdcan1.Init.NominalPrescaler     = CAN_NOMINAL_PRESCALER;
    hfdcan1.Init.NominalSyncJumpWidth = CAN_NOMINAL_SYNC_JUMP_WIDTH;
    hfdcan1.Init.NominalTimeSeg1      = CAN_NOMINAL_TIME_SEG1;
    hfdcan1.Init.NominalTimeSeg2      = CAN_NOMINAL_TIME_SEG2;

    /* Data phase 4 Mbit/s */
    hfdcan1.Init.DataPrescaler        = CAN_DATA_PRESCALER;
    hfdcan1.Init.DataSyncJumpWidth    = CAN_DATA_SYNC_JUMP_WIDTH;
    hfdcan1.Init.DataTimeSeg1         = CAN_DATA_TIME_SEG1;
    hfdcan1.Init.DataTimeSeg2         = CAN_DATA_TIME_SEG2;

    /* Message RAM allocation.
     * The STM32H743 FDCAN message RAM is shared among all FDCAN instances.
     * Allocate from FDCAN1's portion (SRAMCAN base + 0):
     *   Rx FIFO0: 16 elements × 18 words = 288 words
     *   Tx FIFO:  8  elements × 18 words = 144 words
     *   Tx event FIFO: 8 elements × 2 words = 16 words
     */
    hfdcan1.Init.StdFiltersNbr        = 11U;   /* 11 standard ID filters */
    hfdcan1.Init.ExtFiltersNbr        = 0U;
    hfdcan1.Init.RxFifo0ElmtsNbr     = CAN_RX_QUEUE_DEPTH;
    hfdcan1.Init.RxFifo0ElmtSize     = FDCAN_DATA_BYTES_64;
    hfdcan1.Init.RxFifo1ElmtsNbr     = 0U;
    hfdcan1.Init.RxFifo1ElmtSize     = FDCAN_DATA_BYTES_8;
    hfdcan1.Init.RxBuffersNbr        = 0U;
    hfdcan1.Init.TxEventsNbr         = CAN_TX_QUEUE_DEPTH;
    hfdcan1.Init.TxBuffersNbr        = 0U;
    hfdcan1.Init.TxFifoQueueElmtsNbr = CAN_TX_QUEUE_DEPTH;
    hfdcan1.Init.TxFifoQueueMode     = FDCAN_TX_FIFO_OPERATION;
    hfdcan1.Init.TxElmtSize          = FDCAN_DATA_BYTES_64;

    if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK)
    {
        return false;
    }

    /* Configure acceptance filters. */
    can_filter_init();

    /* Enable Rx FIFO0 new-message notification. */
    if (HAL_FDCAN_ActivateNotification(&hfdcan1,
                                       FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0U) != HAL_OK)
    {
        return false;
    }

    /* Enable error notifications for bus-off and warning-level events. */
    if (HAL_FDCAN_ActivateNotification(&hfdcan1,
                                       FDCAN_IT_BUS_OFF        |
                                       FDCAN_IT_ERROR_WARNING  |
                                       FDCAN_IT_ERROR_PASSIVE, 0U) != HAL_OK)
    {
        return false;
    }

    /* Start FDCAN controller — moves from INIT mode to NORMAL mode. */
    if (HAL_FDCAN_Start(&hfdcan1) != HAL_OK)
    {
        return false;
    }

    /* Enable FDCAN interrupt in NVIC. */
    HAL_NVIC_SetPriority(FDCAN1_IT0_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(FDCAN1_IT0_IRQn);

    return true;
}

/* =========================================================================
 * can_filter_init()
 *
 * Configures 11 standard-ID acceptance filters using ID list mode.
 * Each filter element in list mode accepts up to 2 CAN IDs.
 * We fill 6 dual-ID filter elements (covering 11 specific IDs + 1 broadcast)
 * and one element for the broadcast.
 * ========================================================================= */

static void can_filter_init(void)
{
    FDCAN_FilterTypeDef filter = {0};

    filter.IdType       = FDCAN_STANDARD_ID;
    filter.FilterType   = FDCAN_FILTER_DUAL;     /* ID list mode: match ID1 or ID2 */
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;

    /* Filter 0: LSO heartbeat + LSO output status */
    filter.FilterIndex  = 0U;
    filter.FilterID1    = CAN_FILTER_ID_LSO_HEARTBEAT;
    filter.FilterID2    = CAN_FILTER_ID_LSO_OUTPUT_STATUS;
    HAL_FDCAN_ConfigFilter(&hfdcan1, &filter);

    /* Filter 1: LPB heartbeat + LPB battery status */
    filter.FilterIndex  = 1U;
    filter.FilterID1    = CAN_FILTER_ID_LPB_HEARTBEAT;
    filter.FilterID2    = CAN_FILTER_ID_LPB_BATT_STATUS;
    HAL_FDCAN_ConfigFilter(&hfdcan1, &filter);

    /* Filter 2: LPI heartbeat + LPI pedestrian event */
    filter.FilterIndex  = 2U;
    filter.FilterID1    = CAN_FILTER_ID_LPI_HEARTBEAT;
    filter.FilterID2    = CAN_FILTER_ID_LPI_PED_EVENT;
    HAL_FDCAN_ConfigFilter(&hfdcan1, &filter);

    /* Filter 3: Safety supervisor heartbeat + safety fault report */
    filter.FilterIndex  = 3U;
    filter.FilterID1    = CAN_FILTER_ID_SAFETY_HEARTBEAT;
    filter.FilterID2    = CAN_FILTER_ID_SAFETY_FAULT;
    HAL_FDCAN_ConfigFilter(&hfdcan1, &filter);

    /* Filter 4: LSO fault report + LPB fault report */
    filter.FilterIndex  = 4U;
    filter.FilterID1    = CAN_FILTER_ID_LSO_FAULT;
    filter.FilterID2    = CAN_FILTER_ID_LPB_FAULT;
    HAL_FDCAN_ConfigFilter(&hfdcan1, &filter);

    /* Filter 5: Emergency broadcast 0x7FF (ID2 = same, single ID match via mask) */
    filter.FilterIndex  = 5U;
    filter.FilterType   = FDCAN_FILTER_MASK;     /* Exact match: ID1 & mask ID2 */
    filter.FilterID1    = CAN_FILTER_ID_BROADCAST;
    filter.FilterID2    = 0x7FFU;                /* Mask = all bits → exact match */
    HAL_FDCAN_ConfigFilter(&hfdcan1, &filter);

    /*
     * Reject all other standard IDs not matched by the above filters.
     * The FDCAN global filter is set to reject non-matching frames to hardware.
     */
    HAL_FDCAN_ConfigGlobalFilter(&hfdcan1,
                                 FDCAN_REJECT,          /* Non-matching standard IDs */
                                 FDCAN_REJECT,          /* Non-matching extended IDs */
                                 FDCAN_REJECT_REMOTE,   /* Remote frames             */
                                 FDCAN_REJECT_REMOTE);  /* Remote frames (ext)       */
}

/* =========================================================================
 * can_bus_transmit()
 * ========================================================================= */

bool can_bus_transmit(const can_message_t *msg)
{
    if (msg == NULL) { return false; }

    /* Try to push directly into the hardware Tx FIFO first. */
    if (can_hw_transmit(msg))
    {
        g_stats.tx_frames++;
        return true;
    }

    /* Hardware FIFO full — place in software queue. */
    if (xSemaphoreTake(g_tx_mutex, 0U) == pdTRUE)
    {
        uint32_t next_write = (g_tx_write_idx + 1U) & TX_QUEUE_MASK;
        if (next_write != g_tx_read_idx)
        {
            g_tx_queue_buf[g_tx_write_idx] = *msg;
            g_tx_write_idx = next_write;
            xSemaphoreGive(g_tx_mutex);
            return true;
        }
        /* Software queue also full — drop oldest entry to make room. */
        g_stats.tx_errors++;
        g_tx_queue_buf[g_tx_write_idx] = *msg;
        g_tx_write_idx = next_write;
        g_tx_read_idx  = (g_tx_read_idx + 1U) & TX_QUEUE_MASK;
        xSemaphoreGive(g_tx_mutex);
        return false;  /* Dropped an older frame */
    }

    g_stats.tx_errors++;
    return false;
}

/* =========================================================================
 * can_hw_transmit()
 *
 * Attempts to place a message into the FDCAN1 Tx FIFO directly via HAL.
 * Returns true if successful, false if the FIFO was full.
 * ========================================================================= */

static bool can_hw_transmit(const can_message_t *msg)
{
    FDCAN_TxHeaderTypeDef tx_hdr = {0};

    tx_hdr.Identifier          = msg->id & 0x7FFU;
    tx_hdr.IdType              = FDCAN_STANDARD_ID;
    tx_hdr.TxFrameType         = FDCAN_DATA_FRAME;
    tx_hdr.DataLength          = bytes_to_dlc(msg->dlc);
    tx_hdr.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    tx_hdr.BitRateSwitch       = msg->brs ? FDCAN_BRS_ON : FDCAN_BRS_OFF;
    tx_hdr.FDFormat            = msg->is_fd_frame ? FDCAN_FD_CAN : FDCAN_CLASSIC_CAN;
    tx_hdr.TxEventFifoControl  = FDCAN_STORE_TX_EVENTS;
    tx_hdr.MessageMarker       = (uint8_t)(msg->id & 0xFFU);

    /* Clamp DLC to actual buffer. */
    uint8_t byte_count = dlc_to_bytes(tx_hdr.DataLength);

    HAL_StatusTypeDef result = HAL_FDCAN_AddMessageToTxFifo(&hfdcan1,
                                                             &tx_hdr,
                                                             (uint8_t *)msg->data);
    (void)byte_count;
    return (result == HAL_OK);
}

/* =========================================================================
 * can_tx_drain()
 *
 * Drain the software Tx queue into the hardware FIFO.  Called from can_rx_task
 * each iteration so that queued Tx frames are sent as soon as HW space is free.
 * ========================================================================= */

static void can_tx_drain(void)
{
    if (xSemaphoreTake(g_tx_mutex, 0U) != pdTRUE) { return; }

    while (g_tx_read_idx != g_tx_write_idx)
    {
        const can_message_t *msg = &g_tx_queue_buf[g_tx_read_idx];
        if (!can_hw_transmit(msg))
        {
            /* Hardware still full — try next iteration. */
            break;
        }
        g_stats.tx_frames++;
        g_tx_read_idx = (g_tx_read_idx + 1U) & TX_QUEUE_MASK;
    }

    xSemaphoreGive(g_tx_mutex);
}

/* =========================================================================
 * can_bus_register_rx_callback()
 * ========================================================================= */

bool can_bus_register_rx_callback(uint32_t id, can_rx_callback_t callback)
{
    if (callback == NULL) { return false; }

    bool registered = false;

    if (xSemaphoreTake(g_cb_mutex, pdMS_TO_TICKS(10U)) == pdTRUE)
    {
        if (g_rx_callback_count < CAN_MAX_RX_CALLBACKS)
        {
            g_rx_callbacks[g_rx_callback_count].id     = id;
            g_rx_callbacks[g_rx_callback_count].cb     = callback;
            g_rx_callbacks[g_rx_callback_count].active = true;
            g_rx_callback_count++;
            registered = true;
        }
        xSemaphoreGive(g_cb_mutex);
    }

    return registered;
}

/* =========================================================================
 * can_bus_get_tx_queue_depth()
 * ========================================================================= */

uint32_t can_bus_get_tx_queue_depth(void)
{
    /* Ring buffer occupancy: (write - read) wrapped at queue depth. */
    return (g_tx_write_idx - g_tx_read_idx) & TX_QUEUE_MASK;
}

/* =========================================================================
 * can_bus_get_error_state()
 * ========================================================================= */

can_error_state_t can_bus_get_error_state(void)
{
    uint32_t psr = READ_REG(hfdcan1.Instance->PSR);

    uint32_t lec  = (psr & FDCAN_PSR_LEC_Msk)  >> FDCAN_PSR_LEC_Pos;
    uint32_t dlec = (psr & FDCAN_PSR_DLEC_Msk) >> FDCAN_PSR_DLEC_Pos;
    uint32_t ep   = (psr & FDCAN_PSR_EP_Msk)   >> FDCAN_PSR_EP_Pos;
    uint32_t ew   = (psr & FDCAN_PSR_EW_Msk)   >> FDCAN_PSR_EW_Pos;
    uint32_t bo   = (psr & FDCAN_PSR_BO_Msk)   >> FDCAN_PSR_BO_Pos;

    (void)lec; (void)dlec;  /* Used indirectly via EP/EW/BO bits */

    if (bo)
    {
        /* Initiate bus-off recovery. */
        HAL_FDCAN_RecoverFromBusOff(&hfdcan1);

        /* Re-enable Rx interrupt after recovery re-init. */
        HAL_FDCAN_ActivateNotification(&hfdcan1,
                                       FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0U);
        g_stats.bus_off_events++;
        return CAN_ERROR_BUS_OFF;
    }

    if (ep) { return CAN_ERROR_PASSIVE; }
    if (ew) { return CAN_ERROR_WARNING; }

    return CAN_ERROR_NONE;
}

/* =========================================================================
 * can_bus_get_stats()
 * ========================================================================= */

void can_bus_get_stats(can_bus_stats_t *stats_out)
{
    if (stats_out == NULL) { return; }
    /* Atomic copy — each field is 32-bit aligned on Cortex-M; single-word
     * reads are atomic on ARMv7-M without further locking. */
    *stats_out = g_stats;
}

/* =========================================================================
 * can_rx_task()
 *
 * FreeRTOS task: dequeues frames from the ISR-populated ring buffer and
 * dispatches to registered callbacks.  Also drains the Tx queue.
 * ========================================================================= */

void can_rx_task(void *argument)
{
    (void)argument;

    for (;;)
    {
        /* Drain Tx queue on every iteration. */
        can_tx_drain();

        /* Process any frames in the Rx ring buffer. */
        while (g_rx_read_idx != g_rx_write_idx)
        {
            const can_message_t *frame = &g_rx_ring_buf[g_rx_read_idx];

            /* Dispatch to matching callbacks. */
            if (xSemaphoreTake(g_cb_mutex, 0U) == pdTRUE)
            {
                for (uint32_t i = 0U; i < g_rx_callback_count; i++)
                {
                    if (!g_rx_callbacks[i].active) { continue; }

                    if ((g_rx_callbacks[i].id == 0xFFFFFFFFU) ||
                        (g_rx_callbacks[i].id == frame->id))
                    {
                        g_rx_callbacks[i].cb(frame);
                    }
                }
                xSemaphoreGive(g_cb_mutex);
            }

            /* Advance read pointer — done after callback so frame remains valid
             * for the full callback duration. */
            g_rx_read_idx = (g_rx_read_idx + 1U) & RX_RING_MASK;
        }

        /* Yield for CAN_RX_TASK_BLOCK_MS then loop again. */
        vTaskDelay(pdMS_TO_TICKS(CAN_RX_TASK_BLOCK_MS));
    }
}

/* =========================================================================
 * HAL_FDCAN_RxFifo0Callback — Rx interrupt callback (ISR context)
 *
 * Reads one frame from the FDCAN Rx FIFO0 and writes it into the ring buffer.
 * Does NOT call any FreeRTOS API that might cause a context switch.
 * ========================================================================= */

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    if (hfdcan->Instance != FDCAN1) { return; }
    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) == 0U) { return; }

    FDCAN_RxHeaderTypeDef rx_hdr = {0};
    uint8_t               rx_data[CAN_FD_MAX_DLC];

    if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &rx_hdr, rx_data) != HAL_OK)
    {
        return;
    }

    /* Check for ring buffer overflow. */
    uint32_t next_write = (g_rx_write_idx + 1U) & RX_RING_MASK;
    if (next_write == g_rx_read_idx)
    {
        /* Overflow — drop the oldest entry to make room. */
        g_rx_read_idx = (g_rx_read_idx + 1U) & RX_RING_MASK;
        g_stats.rx_overflows++;
    }

    /* Populate the ring buffer entry. */
    can_message_t *entry = &g_rx_ring_buf[g_rx_write_idx];
    entry->id         = rx_hdr.Identifier;
    entry->dlc        = dlc_to_bytes(rx_hdr.DataLength);
    entry->is_fd_frame = (rx_hdr.FDFormat   == FDCAN_FD_CAN);
    entry->brs         = (rx_hdr.BitRateSwitch == FDCAN_BRS_ON);
    entry->timestamp   = can_timestamp_us((uint16_t)rx_hdr.RxTimestamp);
    memcpy(entry->data, rx_data, entry->dlc);

    g_stats.rx_frames++;
    g_rx_write_idx = next_write;
}

/* =========================================================================
 * HAL_FDCAN_ErrorStatusCallback — error status ISR callback
 * ========================================================================= */

void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *hfdcan, uint32_t ErrorStatusITs)
{
    if (hfdcan->Instance != FDCAN1) { return; }

    if (ErrorStatusITs & FDCAN_IT_BUS_OFF)
    {
        g_stats.bus_off_events++;
        /* Full recovery is handled from task context in can_bus_get_error_state(). */
    }
    if (ErrorStatusITs & FDCAN_IT_ERROR_WARNING)
    {
        /* Warning level — no action needed beyond statistics. */
    }
}

/* =========================================================================
 * FDCAN1 IRQ handler
 * ========================================================================= */

void FDCAN1_IT0_IRQHandler(void)
{
    HAL_FDCAN_IRQHandler(&hfdcan1);
}

/* =========================================================================
 * DLC conversion helpers
 *
 * CAN FD DLC codes > 8 do not map linearly to byte counts.
 * FDCAN HAL uses FDCAN_DATA_BYTES_xx constants for the DataLength field.
 * ========================================================================= */

static uint8_t dlc_to_bytes(uint32_t dlc_code)
{
    /* HAL DataLength values for CAN FD (FDCAN_DATA_BYTES_xx macros). */
    switch (dlc_code)
    {
        case FDCAN_DLC_BYTES_0:  return 0U;
        case FDCAN_DLC_BYTES_1:  return 1U;
        case FDCAN_DLC_BYTES_2:  return 2U;
        case FDCAN_DLC_BYTES_3:  return 3U;
        case FDCAN_DLC_BYTES_4:  return 4U;
        case FDCAN_DLC_BYTES_5:  return 5U;
        case FDCAN_DLC_BYTES_6:  return 6U;
        case FDCAN_DLC_BYTES_7:  return 7U;
        case FDCAN_DLC_BYTES_8:  return 8U;
        case FDCAN_DLC_BYTES_12: return 12U;
        case FDCAN_DLC_BYTES_16: return 16U;
        case FDCAN_DLC_BYTES_20: return 20U;
        case FDCAN_DLC_BYTES_24: return 24U;
        case FDCAN_DLC_BYTES_32: return 32U;
        case FDCAN_DLC_BYTES_48: return 48U;
        case FDCAN_DLC_BYTES_64: return 64U;
        default:                 return 8U;   /* Conservative fallback */
    }
}

static uint32_t bytes_to_dlc(uint8_t data_len)
{
    if (data_len == 0U)  { return FDCAN_DLC_BYTES_0;  }
    if (data_len <= 1U)  { return FDCAN_DLC_BYTES_1;  }
    if (data_len <= 2U)  { return FDCAN_DLC_BYTES_2;  }
    if (data_len <= 3U)  { return FDCAN_DLC_BYTES_3;  }
    if (data_len <= 4U)  { return FDCAN_DLC_BYTES_4;  }
    if (data_len <= 5U)  { return FDCAN_DLC_BYTES_5;  }
    if (data_len <= 6U)  { return FDCAN_DLC_BYTES_6;  }
    if (data_len <= 7U)  { return FDCAN_DLC_BYTES_7;  }
    if (data_len <= 8U)  { return FDCAN_DLC_BYTES_8;  }
    if (data_len <= 12U) { return FDCAN_DLC_BYTES_12; }
    if (data_len <= 16U) { return FDCAN_DLC_BYTES_16; }
    if (data_len <= 20U) { return FDCAN_DLC_BYTES_20; }
    if (data_len <= 24U) { return FDCAN_DLC_BYTES_24; }
    if (data_len <= 32U) { return FDCAN_DLC_BYTES_32; }
    if (data_len <= 48U) { return FDCAN_DLC_BYTES_48; }
    return FDCAN_DLC_BYTES_64;
}

/* =========================================================================
 * can_timestamp_us()
 *
 * Converts the 16-bit FDCAN hardware timestamp counter value to a 32-bit
 * microsecond timestamp.  The FDCAN timestamp counter increments at the
 * FDCAN bit clock rate (1 MHz nominal arbitration rate → 1 µs per tick).
 * A software epoch counter extends the range to 32 bits before wrap.
 * ========================================================================= */

static uint32_t can_timestamp_us(uint16_t hw_ts)
{
    static uint16_t last_hw_ts = 0U;

    /* Detect 16-bit counter wrap and increment epoch. */
    if (hw_ts < last_hw_ts)
    {
        g_ts_epoch++;
    }
    last_hw_ts = hw_ts;

    return (g_ts_epoch << 16U) | (uint32_t)hw_ts;
}
