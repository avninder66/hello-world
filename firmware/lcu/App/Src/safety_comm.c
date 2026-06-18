/**
 * @file    safety_comm.c
 * @brief   LCU-100 Safety Supervisor SPI communication driver
 *
 * This module implements the periodic SPI exchange with the STM32G071
 * safety supervisor MCU.  It is the highest-priority application task.
 *
 * Protocol summary (see lumina_safety_protocol.h for full frame spec):
 *   - Every 10 ms: LCU sends heartbeat frame, receives status response.
 *   - On permissive request: LCU sends REQUEST_PHASE frame, waits for
 *     PERMISSIVE_GRANTED/DENIED response (up to SAFETY_SPI_TIMEOUT_MS * 3).
 *   - CRC-8 (polynomial 0x07, SAE J1850) computed on every frame.
 *   - 3 consecutive missed/bad frames → inhibit + SYSEVT_FAULT_ACTIVE.
 *
 * SPI electrical notes:
 *   - SPI2, CPOL=0, CPHA=0 (mode 0), MSB first, 3.75 MHz
 *   - CS active-low, asserted by software (NSS soft)
 *   - 1 µs CS setup/hold met by vTaskDelay(1) which gives ≥1 tick (1 ms)
 *     — sufficient margin over the 1 µs electrical requirement.
 *   - Safety MCU drives MISO within 50 ns of SCK edge at 3.75 MHz.
 */

#include "safety_comm.h"
#include "main.h"
#include "fault_codes.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "event_groups.h"

#include <string.h>
#include <stddef.h>

/* ============================================================================
 * CRC-8 implementation (polynomial 0x07, SAE J1850 / CRC-8/SMBUS)
 * Initial value 0x00, no input/output reflection, no final XOR.
 * ============================================================================ */

/** Pre-computed CRC-8/SMBUS lookup table (polynomial 0x07) */
static const uint8_t k_crc8_table[256] = {
    0x00U, 0x07U, 0x0EU, 0x09U, 0x1CU, 0x1BU, 0x12U, 0x15U,
    0x38U, 0x3FU, 0x36U, 0x31U, 0x24U, 0x23U, 0x2AU, 0x2DU,
    0x70U, 0x77U, 0x7EU, 0x79U, 0x6CU, 0x6BU, 0x62U, 0x65U,
    0x48U, 0x4FU, 0x46U, 0x41U, 0x54U, 0x53U, 0x5AU, 0x5DU,
    0xE0U, 0xE7U, 0xEEU, 0xE9U, 0xFCU, 0xFBU, 0xF2U, 0xF5U,
    0xD8U, 0xDFU, 0xD6U, 0xD1U, 0xC4U, 0xC3U, 0xCAU, 0xCDU,
    0x90U, 0x97U, 0x9EU, 0x99U, 0x8CU, 0x8BU, 0x82U, 0x85U,
    0xA8U, 0xAFU, 0xA6U, 0xA1U, 0xB4U, 0xB3U, 0xBAU, 0xBDU,
    0xC7U, 0xC0U, 0xC9U, 0xCEU, 0xDBU, 0xDCU, 0xD5U, 0xD2U,
    0xFFU, 0xF8U, 0xF1U, 0xF6U, 0xE3U, 0xE4U, 0xEDU, 0xEAU,
    0xB7U, 0xB0U, 0xB9U, 0xBEU, 0xABU, 0xACU, 0xA5U, 0xA2U,
    0x8FU, 0x88U, 0x81U, 0x86U, 0x93U, 0x94U, 0x9DU, 0x9AU,
    0x27U, 0x20U, 0x29U, 0x2EU, 0x3BU, 0x3CU, 0x35U, 0x32U,
    0x1FU, 0x18U, 0x11U, 0x16U, 0x03U, 0x04U, 0x0DU, 0x0AU,
    0x57U, 0x50U, 0x59U, 0x5EU, 0x4BU, 0x4CU, 0x45U, 0x42U,
    0x6FU, 0x68U, 0x61U, 0x66U, 0x73U, 0x74U, 0x7DU, 0x7AU,
    0x89U, 0x8EU, 0x87U, 0x80U, 0x95U, 0x92U, 0x9BU, 0x9CU,
    0xB1U, 0xB6U, 0xBFU, 0xB8U, 0xADU, 0xAAU, 0xA3U, 0xA4U,
    0xF9U, 0xFEU, 0xF7U, 0xF0U, 0xE5U, 0xE2U, 0xEBU, 0xECU,
    0xC1U, 0xC6U, 0xCFU, 0xC8U, 0xDDU, 0xDAU, 0xD3U, 0xD4U,
    0x69U, 0x6EU, 0x67U, 0x60U, 0x75U, 0x72U, 0x7BU, 0x7CU,
    0x51U, 0x56U, 0x5FU, 0x58U, 0x4DU, 0x4AU, 0x43U, 0x44U,
    0x19U, 0x1EU, 0x17U, 0x10U, 0x05U, 0x02U, 0x0BU, 0x0CU,
    0x21U, 0x26U, 0x2FU, 0x28U, 0x3DU, 0x3AU, 0x33U, 0x34U,
    0x4EU, 0x49U, 0x40U, 0x47U, 0x52U, 0x55U, 0x5CU, 0x5BU,
    0x76U, 0x71U, 0x78U, 0x7FU, 0x6AU, 0x6DU, 0x64U, 0x63U,
    0x3EU, 0x39U, 0x30U, 0x37U, 0x22U, 0x25U, 0x2CU, 0x2BU,
    0x06U, 0x01U, 0x08U, 0x0FU, 0x1AU, 0x1DU, 0x14U, 0x13U,
    0xAEU, 0xA9U, 0xA0U, 0xA7U, 0xB2U, 0xB5U, 0xBCU, 0xBBU,
    0x96U, 0x91U, 0x98U, 0x9FU, 0x8AU, 0x8DU, 0x84U, 0x83U,
    0xDEU, 0xD9U, 0xD0U, 0xD7U, 0xC2U, 0xC5U, 0xCCU, 0xCBU,
    0xE6U, 0xE1U, 0xE8U, 0xEFU, 0xFAU, 0xFDU, 0xF4U, 0xF3U,
};

/**
 * @brief  Compute CRC-8/SMBUS over a byte buffer.
 *
 * @param[in] data  Pointer to input bytes.
 * @param[in] len   Number of bytes to process.
 * @return          8-bit CRC value.
 */
static uint8_t crc8_compute(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00U;
    for (size_t i = 0; i < len; i++) {
        crc = k_crc8_table[crc ^ data[i]];
    }
    return crc;
}

/* ============================================================================
 * Module-level state
 * ============================================================================ */

/** SPI2 mutex — exported for traffic_engine if compound locking is needed */
SemaphoreHandle_t xSPIMutex = NULL;

/** Live status — updated by the task, snapshotted by safety_comm_get_status() */
static safety_status_t s_status;

/** Running sequence counter — wraps at 0xFF */
static uint8_t s_sequence = 0U;

/* ============================================================================
 * Private helpers
 * ============================================================================ */

/**
 * @brief  Assert SAFETY_MCU_CS (active low) with a setup delay.
 */
static inline void cs_assert(void)
{
    HAL_GPIO_WritePin(SAFETY_MCU_CS_GPIO_Port, SAFETY_MCU_CS_Pin, GPIO_PIN_RESET);
    /* 1 µs minimum CS setup time — vTaskDelay(1) gives ≥1 ms, well within spec */
    vTaskDelay(1U);
}

/**
 * @brief  Deassert SAFETY_MCU_CS with a hold delay.
 */
static inline void cs_deassert(void)
{
    vTaskDelay(1U);  /* 1 µs minimum hold time */
    HAL_GPIO_WritePin(SAFETY_MCU_CS_GPIO_Port, SAFETY_MCU_CS_Pin, GPIO_PIN_SET);
}

/**
 * @brief  Build a command frame, fill in sequence and CRC, then send it
 *         over SPI2 and receive the response in full-duplex.
 *
 * @param[in]  opcode          Opcode byte for the command frame.
 * @param[in]  payload0        Byte 1 of command (e.g. phase_id).
 * @param[in]  payload1        Byte 2 of command (e.g. demand_flags).
 * @param[out] rsp_out         Buffer to receive the 8-byte response frame.
 * @return     HAL_OK on success, HAL_TIMEOUT or HAL_ERROR on failure.
 */
static HAL_StatusTypeDef spi_exchange(uint8_t  opcode,
                                       uint8_t  payload0,
                                       uint8_t  payload1,
                                       safety_rsp_frame_t *rsp_out)
{
    safety_cmd_frame_t cmd;
    memset(&cmd, 0, sizeof(cmd));

    cmd.opcode             = opcode;
    cmd.requested_phase_id = payload0;
    cmd.demand_flags       = payload1;
    cmd.reserved[0]        = 0U;
    cmd.reserved[1]        = 0U;
    cmd.sequence           = s_sequence++;
    cmd.crc8               = crc8_compute((const uint8_t *)&cmd,
                                           SAFETY_CRC_IDX);   /* bytes 0-5 */
    cmd.terminator         = SAFETY_FRAME_TERMINATOR;

    cs_assert();

    HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(
        &hspi2,
        (uint8_t *)&cmd,
        (uint8_t *)rsp_out,
        SAFETY_FRAME_LEN,
        SAFETY_SPI_TIMEOUT_MS);

    cs_deassert();

    return status;
}

/**
 * @brief  Validate a received response frame (terminator + CRC check).
 *
 * @param[in] rsp  Pointer to the received response frame.
 * @return true if the frame is structurally valid.
 */
static bool validate_response(const safety_rsp_frame_t *rsp)
{
    if (rsp->terminator != SAFETY_FRAME_TERMINATOR) {
        return false;
    }

    uint8_t expected_crc = crc8_compute((const uint8_t *)rsp, SAFETY_CRC_IDX);
    if (rsp->crc8 != expected_crc) {
        return false;
    }

    return true;
}

/**
 * @brief  Update s_status from a validated response frame.
 *         Called while xSPIMutex is held.
 */
static void update_status_from_response(const safety_rsp_frame_t *rsp)
{
    s_status.state                  = (safety_mcu_state_t)rsp->safety_state;
    s_status.last_permissive_result = (permissive_result_t)rsp->permissive_result;
    s_status.fault_flags            = (uint16_t)((rsp->fault_flags_high << 8) |
                                                   rsp->fault_flags_low);
    s_status.watchdog_count++;
    s_status.miss_count = 0U;
}

/**
 * @brief  Handle a communication miss — increment counter, trigger fault if
 *         threshold reached.  Called while xSPIMutex is held.
 */
static void handle_miss(void)
{
    s_status.miss_count++;

    if (s_status.miss_count >= SAFETY_MAX_MISS_COUNT) {
        /* Safety comms lost — inhibit everything */
        HAL_GPIO_WritePin(INHIBIT_LINE_GPIO_Port, INHIBIT_LINE_Pin, GPIO_PIN_SET);
        xEventGroupSetBits(xSystemEventGroup, SYSEVT_FAULT_ACTIVE);

        /* Log fault via the fault queue (non-blocking) */
        fault_event_t evt = {
            .code         = FAULT_SAFETY_COMM_TIMEOUT,
            .severity     = FAULT_SEV_FATAL,
            .source       = FAULT_SRC_SAFETY_COMM,
            .timestamp_ms = HAL_GetTick(),
            .context      = { s_status.miss_count, s_status.watchdog_count }
        };
        /* xFaultQueue may not be available during early init — guard it */
        if (xFaultQueue != NULL) {
            xQueueSend(xFaultQueue, &evt, 0);
        }
    }
}

/* ============================================================================
 * safety_comm_init
 * ============================================================================ */
bool safety_comm_init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    s_sequence = 0U;

    xSPIMutex = xSemaphoreCreateMutex();
    return (xSPIMutex != NULL);
}

/* ============================================================================
 * safety_comm_task — FreeRTOS task entry point
 *
 * Runs at the highest application priority.  Each iteration:
 *   1. Acquire SPI mutex
 *   2. Send heartbeat frame
 *   3. Validate response
 *   4. Update status / handle miss
 *   5. Release mutex
 *   6. Sleep until next 10 ms tick
 * ============================================================================ */
void safety_comm_task(void *pvParameters)
{
    (void)pvParameters;

    if (xSPIMutex == NULL) {
        if (!safety_comm_init()) {
            /* Cannot operate without mutex */
            vTaskSuspend(NULL);
            return;
        }
    }

    TickType_t       xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod       = pdMS_TO_TICKS(SAFETY_HEARTBEAT_PERIOD_MS);

    for (;;) {
        safety_rsp_frame_t rsp;
        memset(&rsp, 0, sizeof(rsp));

        if (xSemaphoreTake(xSPIMutex, pdMS_TO_TICKS(SAFETY_SPI_TIMEOUT_MS)) == pdTRUE) {

            HAL_StatusTypeDef xfer_status = spi_exchange(
                SAFETY_OP_HEARTBEAT,
                s_status.state == SAFETY_STATE_INHIBIT_ACTIVE ? 0x01U : 0x00U,
                0x00U,
                &rsp);

            if (xfer_status == HAL_OK && validate_response(&rsp)) {
                update_status_from_response(&rsp);

                /* If safety MCU just completed its self-test, signal ready */
                if (rsp.safety_state == (uint8_t)SAFETY_STATE_READY &&
                    !(xEventGroupGetBits(xSystemEventGroup) & SYSEVT_SAFETY_READY)) {
                    xEventGroupSetBits(xSystemEventGroup, SYSEVT_SAFETY_READY);
                }

                /* Propagate safety MCU faults to system event group */
                if (s_status.fault_flags != 0U) {
                    xEventGroupSetBits(xSystemEventGroup, SYSEVT_FAULT_ACTIVE);

                    fault_event_t evt = {
                        .code         = FAULT_SAFETY_SELF_TEST_FAIL,
                        .severity     = FAULT_SEV_CRITICAL,
                        .source       = FAULT_SRC_SAFETY_COMM,
                        .timestamp_ms = HAL_GetTick(),
                        .context      = { s_status.fault_flags, 0 }
                    };
                    if (xFaultQueue != NULL) {
                        xQueueSend(xFaultQueue, &evt, 0);
                    }
                }
            } else {
                /* CRC failure or SPI error counts as a miss */
                if (xfer_status == HAL_OK) {
                    /* Frame received but CRC/terminator invalid */
                    fault_event_t crc_fault = {
                        .code         = FAULT_SAFETY_COMM_CRC,
                        .severity     = FAULT_SEV_WARNING,
                        .source       = FAULT_SRC_SAFETY_COMM,
                        .timestamp_ms = HAL_GetTick(),
                        .context      = { rsp.crc8,
                                          crc8_compute((const uint8_t *)&rsp,
                                                       SAFETY_CRC_IDX) }
                    };
                    if (xFaultQueue != NULL) {
                        xQueueSend(xFaultQueue, &crc_fault, 0);
                    }
                }
                handle_miss();
            }

            xSemaphoreGive(xSPIMutex);
        } else {
            /* Could not acquire mutex within timeout — count as miss */
            handle_miss();
        }

        vTaskDelayUntil(&xLastWakeTime, xPeriod);
    }
}

/* ============================================================================
 * safety_comm_request_permissive
 * ============================================================================ */
permissive_result_t safety_comm_request_permissive(uint8_t requested_phase)
{
    safety_rsp_frame_t rsp;
    memset(&rsp, 0, sizeof(rsp));

    if (xSPIMutex == NULL) {
        return PERMISSIVE_DENIED_FAULT;
    }

    if (xSemaphoreTake(xSPIMutex, pdMS_TO_TICKS(SAFETY_SPI_TIMEOUT_MS * 2U)) != pdTRUE) {
        return PERMISSIVE_DENIED_FAULT;
    }

    HAL_StatusTypeDef xfer_status = spi_exchange(
        SAFETY_OP_REQUEST_PHASE,
        requested_phase,
        0x00U,
        &rsp);

    permissive_result_t result = PERMISSIVE_DENIED_FAULT;

    if (xfer_status == HAL_OK && validate_response(&rsp)) {
        update_status_from_response(&rsp);
        result = (permissive_result_t)rsp.permissive_result;
    } else {
        handle_miss();
    }

    xSemaphoreGive(xSPIMutex);
    return result;
}

/* ============================================================================
 * safety_comm_get_status
 * ============================================================================ */
void safety_comm_get_status(safety_status_t *status_out)
{
    if (status_out == NULL) {
        return;
    }

    if (xSPIMutex != NULL &&
        xSemaphoreTake(xSPIMutex, pdMS_TO_TICKS(5U)) == pdTRUE) {
        *status_out = s_status;    /* struct copy */
        xSemaphoreGive(xSPIMutex);
    } else {
        *status_out = s_status;    /* best-effort read if mutex unavailable */
    }
}

/* ============================================================================
 * safety_comm_trigger_self_test
 * ============================================================================ */
bool safety_comm_trigger_self_test(void)
{
    safety_rsp_frame_t rsp;
    memset(&rsp, 0, sizeof(rsp));

    if (xSPIMutex == NULL) {
        return false;
    }

    /* Send self-test trigger */
    if (xSemaphoreTake(xSPIMutex, pdMS_TO_TICKS(SAFETY_SPI_TIMEOUT_MS * 2U)) != pdTRUE) {
        return false;
    }

    HAL_StatusTypeDef xfer_status = spi_exchange(
        SAFETY_OP_SELF_TEST,
        0x00U, 0x00U,
        &rsp);

    xSemaphoreGive(xSPIMutex);

    if (xfer_status != HAL_OK || !validate_response(&rsp)) {
        return false;
    }

    /* Poll for self-test completion */
    uint32_t start   = HAL_GetTick();
    bool     passed  = false;

    while ((HAL_GetTick() - start) < SAFETY_SELF_TEST_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(SAFETY_SELF_TEST_POLL_MS));

        memset(&rsp, 0, sizeof(rsp));

        if (xSemaphoreTake(xSPIMutex, pdMS_TO_TICKS(SAFETY_SPI_TIMEOUT_MS)) != pdTRUE) {
            continue;
        }

        xfer_status = spi_exchange(SAFETY_OP_HEARTBEAT, 0x00U, 0x00U, &rsp);

        if (xfer_status == HAL_OK && validate_response(&rsp)) {
            update_status_from_response(&rsp);
            if ((safety_mcu_state_t)rsp.safety_state == SAFETY_STATE_READY &&
                s_status.fault_flags == 0U) {
                s_status.self_test_result = 0U;   /* pass */
                passed = true;
                xSemaphoreGive(xSPIMutex);
                break;
            }
            if ((safety_mcu_state_t)rsp.safety_state == SAFETY_STATE_FAULT ||
                (safety_mcu_state_t)rsp.safety_state == SAFETY_STATE_FAULT_LOCKOUT) {
                s_status.self_test_result = (uint8_t)s_status.fault_flags;
                xSemaphoreGive(xSPIMutex);
                break;
            }
        } else {
            handle_miss();
        }

        xSemaphoreGive(xSPIMutex);
    }

    if (passed) {
        /* Clear safety-related fault bits */
        xEventGroupClearBits(xSystemEventGroup, SYSEVT_FAULT_ACTIVE);
        xEventGroupSetBits(xSystemEventGroup,   SYSEVT_SAFETY_READY);
    } else {
        fault_event_t evt = {
            .code         = FAULT_SAFETY_SELF_TEST_FAIL,
            .severity     = FAULT_SEV_FATAL,
            .source       = FAULT_SRC_SAFETY_COMM,
            .timestamp_ms = HAL_GetTick(),
            .context      = { s_status.self_test_result, 0 }
        };
        if (xFaultQueue != NULL) {
            xQueueSend(xFaultQueue, &evt, 0);
        }
    }

    return passed;
}

/* ============================================================================
 * safety_comm_emergency_inhibit
 * ============================================================================ */
void safety_comm_emergency_inhibit(void)
{
    /* Assert the hardware inhibit line immediately — no mutex needed for GPIO */
    HAL_GPIO_WritePin(INHIBIT_LINE_GPIO_Port, INHIBIT_LINE_Pin, GPIO_PIN_SET);

    /* Also tell the safety MCU to latch inhibit */
    if (xSPIMutex == NULL) {
        return;
    }

    safety_rsp_frame_t rsp;
    memset(&rsp, 0, sizeof(rsp));

    /* Best-effort: if we can't get the mutex immediately, GPIO inhibit is
     * already asserted so the hardware state is safe.                      */
    if (xSemaphoreTake(xSPIMutex, 0) == pdTRUE) {
        spi_exchange(SAFETY_OP_INHIBIT, 0x00U, 0x00U, &rsp);
        xSemaphoreGive(xSPIMutex);
    }
}
