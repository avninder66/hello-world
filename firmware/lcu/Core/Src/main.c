/**
 * @file    main.c
 * @brief   LCU-100 Main Controller - application entry point
 *
 * Lumina Temporary Traffic Signal Platform
 * MCU: STM32H743ZIT6  |  RTOS: FreeRTOS v10.4.x
 *
 * Boot sequence
 * 1. HAL and clock initialisation
 * 2. Peripheral initialisation (FDCAN, SPI, UART, I2C, RTC, IWDG)
 * 3. GPIO initialisation (LED, inhibit, door-switch)
 * 4. FreeRTOS object creation (queues, event groups, mutexes)
 * 5. Task creation
 * 6. vTaskStartScheduler() — never returns
 *
 * Copyright (c) Lumina Traffic Systems Ltd.  All rights reserved.
 * SPDX-License-Identifier: Proprietary
 */

#include "main.h"
#include "traffic_engine.h"
#include "safety_comm.h"
#include "fault_codes.h"

/* ============================================================================
 * Private constants
 * ============================================================================ */

/* Task stack sizes (in 32-bit words) */
#define TASK_STACK_SAFETY_COMM      512U
#define TASK_STACK_TRAFFIC_ENGINE   1024U
#define TASK_STACK_CAN_RX           512U
#define TASK_STACK_TELEMETRY        2048U
#define TASK_STACK_FAULT_LOG        256U

/* Queue depths */
#define QUEUE_DEPTH_PHASE_CMD       4U
#define QUEUE_DEPTH_FAULT           16U
#define QUEUE_DEPTH_TELEMETRY       8U

/* IWDG reload value for ~1 s window (32 kHz LSI / 256 prescaler) */
#define IWDG_RELOAD_COUNT           125U

/* ============================================================================
 * Peripheral handles (extern-declared in main.h)
 * ============================================================================ */
FDCAN_HandleTypeDef  hfdcan1;
SPI_HandleTypeDef    hspi1;
SPI_HandleTypeDef    hspi2;
UART_HandleTypeDef   huart4;
UART_HandleTypeDef   huart3;
I2C_HandleTypeDef    hi2c1;
RTC_HandleTypeDef    hrtc;
IWDG_HandleTypeDef   hiwdg;

/* ============================================================================
 * FreeRTOS handles (extern-declared in main.h)
 * ============================================================================ */
TaskHandle_t     hTrafficEngineTask = NULL;
TaskHandle_t     hSafetyCommTask    = NULL;
TaskHandle_t     hCANRxTask         = NULL;
TaskHandle_t     hTelemetryTask     = NULL;
TaskHandle_t     hFaultLogTask      = NULL;

QueueHandle_t    xPhaseCommandQueue = NULL;
QueueHandle_t    xFaultQueue        = NULL;
QueueHandle_t    xTelemetryQueue    = NULL;

EventGroupHandle_t xSystemEventGroup = NULL;

/* ============================================================================
 * Forward declarations (static functions)
 * ============================================================================ */
static void vCANRxTask(void *pvParameters);
static void vTelemetryTask(void *pvParameters);
static void vFaultLogTask(void *pvParameters);

/* Telemetry record written to the telemetry queue */
typedef struct {
    uint32_t timestamp_ms;
    uint8_t  phase_id;
    uint8_t  state;
    uint16_t battery_mv;
    int16_t  temperature_cdeg;   /* 0.01 °C units */
    uint32_t fault_flags;
} telemetry_record_t;

/* CAN phase command message (placed on xPhaseCommandQueue by traffic engine) */
typedef struct {
    uint8_t  approach_id;
    uint8_t  aspect;             /* signal_aspect_t */
    uint32_t duration_ms;
    uint8_t  phase_id;
} phase_cmd_msg_t;

/* ============================================================================
 * main()
 * ============================================================================ */
int main(void)
{
    /* -----------------------------------------------------------------
     * 1. HAL initialisation — must be the very first call
     * ----------------------------------------------------------------- */
    HAL_Init();

    /* -----------------------------------------------------------------
     * 2. System clock: 480 MHz from PLL1 fed by 25 MHz HSE oscillator
     * ----------------------------------------------------------------- */
    SystemClock_Config();

    /* -----------------------------------------------------------------
     * 3. Peripheral initialisation
     * ----------------------------------------------------------------- */
    MX_GPIO_Init();
    MX_FDCAN1_Init();
    MX_SPI1_Init();
    MX_SPI2_Init();
    MX_UART4_Init();
    MX_USART3_Init();
    MX_I2C1_Init();
    MX_RTC_Init();
    MX_IWDG_Init();

    /* Assert inhibit line immediately so no LSO output fires before the
     * safety supervisor grants the first permissive.                   */
    HAL_GPIO_WritePin(INHIBIT_LINE_GPIO_Port, INHIBIT_LINE_Pin, GPIO_PIN_SET);

    /* Drive CAN transceiver out of shutdown */
    HAL_GPIO_WritePin(CAN_SHDN_GPIO_Port, CAN_SHDN_Pin, GPIO_PIN_RESET);

    /* Deassert safety MCU reset (let it boot) */
    HAL_GPIO_WritePin(SAFETY_MCU_RESET_GPIO_Port, SAFETY_MCU_RESET_Pin, GPIO_PIN_SET);

    /* -----------------------------------------------------------------
     * 4. FreeRTOS object creation
     * ----------------------------------------------------------------- */

    /* Event group */
    xSystemEventGroup = xEventGroupCreate();
    if (xSystemEventGroup == NULL) {
        Error_Handler();
    }

    /* Queues */
    xPhaseCommandQueue = xQueueCreate(QUEUE_DEPTH_PHASE_CMD,  sizeof(phase_cmd_msg_t));
    xFaultQueue        = xQueueCreate(QUEUE_DEPTH_FAULT,      sizeof(fault_event_t));
    xTelemetryQueue    = xQueueCreate(QUEUE_DEPTH_TELEMETRY,  sizeof(telemetry_record_t));

    if ((xPhaseCommandQueue == NULL) ||
        (xFaultQueue        == NULL) ||
        (xTelemetryQueue    == NULL)) {
        Error_Handler();
    }

    /* -----------------------------------------------------------------
     * 5. Task creation
     *
     * Priority mapping (higher number = higher priority in FreeRTOS):
     *   osPriorityRealtime  → configMAX_PRIORITIES - 1  (e.g. 7 if MAX=8)
     *   osPriorityHigh      → configMAX_PRIORITIES - 2
     *   osPriorityNormal    → tskIDLE_PRIORITY + 2
     *   osPriorityLow       → tskIDLE_PRIORITY + 1
     * ----------------------------------------------------------------- */
    BaseType_t xRet;

    /* SafetyComm — highest application priority, 10 ms hard deadline */
    xRet = xTaskCreate(
        safety_comm_task,
        "SafetyComm",
        TASK_STACK_SAFETY_COMM,
        NULL,
        (configMAX_PRIORITIES - 1),
        &hSafetyCommTask);
    if (xRet != pdPASS) { Error_Handler(); }

    /* TrafficEngine — state machine, 50 ms tick */
    xRet = xTaskCreate(
        traffic_engine_task,
        "TrafficEngine",
        TASK_STACK_TRAFFIC_ENGINE,
        NULL,
        (configMAX_PRIORITIES - 2),
        &hTrafficEngineTask);
    if (xRet != pdPASS) { Error_Handler(); }

    /* CANRx — processes inbound FDCAN frames */
    xRet = xTaskCreate(
        vCANRxTask,
        "CANRx",
        TASK_STACK_CAN_RX,
        NULL,
        (configMAX_PRIORITIES - 2),
        &hCANRxTask);
    if (xRet != pdPASS) { Error_Handler(); }

    /* Telemetry — assembles and transmits status reports */
    xRet = xTaskCreate(
        vTelemetryTask,
        "Telemetry",
        TASK_STACK_TELEMETRY,
        NULL,
        (tskIDLE_PRIORITY + 2U),
        &hTelemetryTask);
    if (xRet != pdPASS) { Error_Handler(); }

    /* FaultLog — drains xFaultQueue and persists events to FRAM */
    xRet = xTaskCreate(
        vFaultLogTask,
        "FaultLog",
        TASK_STACK_FAULT_LOG,
        NULL,
        (tskIDLE_PRIORITY + 1U),
        &hFaultLogTask);
    if (xRet != pdPASS) { Error_Handler(); }

    /* -----------------------------------------------------------------
     * 6. Start scheduler — never returns
     * ----------------------------------------------------------------- */
    vTaskStartScheduler();

    /* Should never reach here.  If we do the heap was exhausted. */
    Error_Handler();
    for (;;) {}
}

/* ============================================================================
 * SystemClock_Config
 *
 * HSE 25 MHz → PLL1 → SYSCLK 480 MHz
 *   PLL1: M=5, N=192, P=2  →  (25/5)*192/2 = 480 MHz  SYSCLK
 *   AHB  prescaler /1 → HCLK  480 MHz
 *   APB1 prescaler /4 → PCLK1 120 MHz
 *   APB2 prescaler /2 → PCLK2 240 MHz
 * ============================================================================ */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef       RCC_OscInit  = {0};
    RCC_ClkInitTypeDef       RCC_ClkInit  = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    /* Supply configuration — set VOS0 (highest performance) */
    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

    RCC_OscInit.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_LSI;
    RCC_OscInit.HSEState       = RCC_HSE_ON;
    RCC_OscInit.LSIState       = RCC_LSI_ON;    /* Required by IWDG */
    RCC_OscInit.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInit.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInit.PLL.PLLM       = 5;
    RCC_OscInit.PLL.PLLN       = 192;
    RCC_OscInit.PLL.PLLP       = 2;
    RCC_OscInit.PLL.PLLQ       = 4;    /* 60 MHz for FDCAN */
    RCC_OscInit.PLL.PLLR       = 2;
    RCC_OscInit.PLL.PLLRGE     = RCC_PLL1VCIRANGE_2;
    RCC_OscInit.PLL.PLLVCOSEL  = RCC_PLL1VCOWIDE;
    RCC_OscInit.PLL.PLLFRACN   = 0;

    if (HAL_RCC_OscConfig(&RCC_OscInit) != HAL_OK) {
        Error_Handler();
    }

    RCC_ClkInit.ClockType = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK  |
                              RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2 |
                              RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1);
    RCC_ClkInit.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInit.SYSCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInit.AHBCLKDivider  = RCC_HCLK_DIV2;   /* 240 MHz AHB */
    RCC_ClkInit.APB1CLKDivider = RCC_APB1_DIV2;
    RCC_ClkInit.APB2CLKDivider = RCC_APB2_DIV2;
    RCC_ClkInit.APB3CLKDivider = RCC_APB3_DIV2;
    RCC_ClkInit.APB4CLKDivider = RCC_APB4_DIV2;

    /* 4 wait states needed at 480 MHz (see DS reference manual) */
    if (HAL_RCC_ClockConfig(&RCC_ClkInit, FLASH_LATENCY_4) != HAL_OK) {
        Error_Handler();
    }

    /* FDCAN kernel clock from PLL1Q (60 MHz) */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_FDCAN;
    PeriphClkInit.FdcanClockSelection  = RCC_FDCANCLKSOURCE_PLL;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
        Error_Handler();
    }
}

/* ============================================================================
 * MX_GPIO_Init
 * ============================================================================ */
void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_Init = {0};

    /* Enable GPIO clocks */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();

    /* --- Output defaults -------------------------------------------------- */
    /* LEDs off */
    HAL_GPIO_WritePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_FAULT_GPIO_Port,  LED_FAULT_Pin,  GPIO_PIN_RESET);

    /* SPI CS lines deasserted (high) */
    HAL_GPIO_WritePin(SAFETY_MCU_CS_GPIO_Port, SAFETY_MCU_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(FRAM_CS_GPIO_Port,       FRAM_CS_Pin,       GPIO_PIN_SET);

    /* Safety MCU held in reset initially */
    HAL_GPIO_WritePin(SAFETY_MCU_RESET_GPIO_Port, SAFETY_MCU_RESET_Pin, GPIO_PIN_RESET);

    /* Inhibit line asserted (active high) */
    HAL_GPIO_WritePin(INHIBIT_LINE_GPIO_Port, INHIBIT_LINE_Pin, GPIO_PIN_SET);

    /* CAN transceiver in shutdown (active high) */
    HAL_GPIO_WritePin(CAN_SHDN_GPIO_Port, CAN_SHDN_Pin, GPIO_PIN_SET);

    /* --- Configure LED_STATUS (PB0) --------------------------------------- */
    GPIO_Init.Pin   = LED_STATUS_Pin;
    GPIO_Init.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_Init.Pull  = GPIO_NOPULL;
    GPIO_Init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_STATUS_GPIO_Port, &GPIO_Init);

    /* --- Configure LED_FAULT (PB1) ---------------------------------------- */
    GPIO_Init.Pin = LED_FAULT_Pin;
    HAL_GPIO_Init(LED_FAULT_GPIO_Port, &GPIO_Init);

    /* --- Configure SAFETY_MCU_CS (PC6) ------------------------------------ */
    GPIO_Init.Pin   = SAFETY_MCU_CS_Pin;
    GPIO_Init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SAFETY_MCU_CS_GPIO_Port, &GPIO_Init);

    /* --- Configure SAFETY_MCU_RESET (PC7) --------------------------------- */
    GPIO_Init.Pin   = SAFETY_MCU_RESET_Pin;
    GPIO_Init.Mode  = GPIO_MODE_OUTPUT_OD;   /* Open-drain: safety MCU has ext pull-up */
    GPIO_Init.Pull  = GPIO_NOPULL;
    GPIO_Init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SAFETY_MCU_RESET_GPIO_Port, &GPIO_Init);

    /* --- Configure CAN_SHDN (PA8) ----------------------------------------- */
    GPIO_Init.Pin   = CAN_SHDN_Pin;
    GPIO_Init.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_Init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(CAN_SHDN_GPIO_Port, &GPIO_Init);

    /* --- Configure USB_VBUS sense (PA9, input) ----------------------------- */
    GPIO_Init.Pin  = USB_VBUS_Pin;
    GPIO_Init.Mode = GPIO_MODE_INPUT;
    GPIO_Init.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(USB_VBUS_GPIO_Port, &GPIO_Init);

    /* --- Configure FRAM_CS (PD14) ----------------------------------------- */
    GPIO_Init.Pin   = FRAM_CS_Pin;
    GPIO_Init.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_Init.Pull  = GPIO_NOPULL;
    GPIO_Init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(FRAM_CS_GPIO_Port, &GPIO_Init);

    /* --- Configure INHIBIT_LINE (PE2, output high) ------------------------ */
    GPIO_Init.Pin   = INHIBIT_LINE_Pin;
    GPIO_Init.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_Init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(INHIBIT_LINE_GPIO_Port, &GPIO_Init);

    /* --- Configure DOOR_SWITCH (PF0, input with pull-down) ---------------- */
    GPIO_Init.Pin  = DOOR_SWITCH_Pin;
    GPIO_Init.Mode = GPIO_MODE_INPUT;
    GPIO_Init.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(DOOR_SWITCH_GPIO_Port, &GPIO_Init);
}

/* ============================================================================
 * MX_FDCAN1_Init
 *
 * 500 kbit/s nominal, 2 Mbit/s data phase (FDCAN FD mode)
 * Kernel clock: 60 MHz from PLL1Q
 * Prescaler 1, NomTimeSeg1=119, NomTimeSeg2=40 → 500 kbit/s
 * DataPrescaler 1, DataTimeSeg1=29, DataTimeSeg2=10 → 2 Mbit/s
 * ============================================================================ */
void MX_FDCAN1_Init(void)
{
    hfdcan1.Instance                  = FDCAN1;
    hfdcan1.Init.FrameFormat          = FDCAN_FRAME_FD_BRS;
    hfdcan1.Init.Mode                 = FDCAN_MODE_NORMAL;
    hfdcan1.Init.AutoRetransmission   = ENABLE;
    hfdcan1.Init.TransmitPause        = DISABLE;
    hfdcan1.Init.ProtocolException    = ENABLE;
    hfdcan1.Init.NominalPrescaler     = 1;
    hfdcan1.Init.NominalSyncJumpWidth = 16;
    hfdcan1.Init.NominalTimeSeg1      = 119;
    hfdcan1.Init.NominalTimeSeg2      = 40;
    hfdcan1.Init.DataPrescaler        = 1;
    hfdcan1.Init.DataSyncJumpWidth    = 4;
    hfdcan1.Init.DataTimeSeg1         = 29;
    hfdcan1.Init.DataTimeSeg2         = 10;
    hfdcan1.Init.MessageRAMOffset     = 0;
    hfdcan1.Init.StdFiltersNbr        = 8;
    hfdcan1.Init.ExtFiltersNbr        = 4;
    hfdcan1.Init.RxFifo0ElmtsNbr     = 8;
    hfdcan1.Init.RxFifo0ElmtSize     = FDCAN_DATA_BYTES_8;
    hfdcan1.Init.RxFifo1ElmtsNbr     = 4;
    hfdcan1.Init.RxFifo1ElmtSize     = FDCAN_DATA_BYTES_64;
    hfdcan1.Init.RxBuffersNbr        = 0;
    hfdcan1.Init.TxEventsNbr         = 4;
    hfdcan1.Init.TxBuffersNbr        = 2;
    hfdcan1.Init.TxFifoQueueElmtsNbr = 8;
    hfdcan1.Init.TxFifoQueueMode     = FDCAN_TX_FIFO_OPERATION;
    hfdcan1.Init.TxElmtSize          = FDCAN_DATA_BYTES_64;

    if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK) {
        Error_Handler();
    }
}

/* ============================================================================
 * MX_SPI1_Init  — FRAM (MB85RS2MT), SPI mode 0, max 40 MHz
 * APB2 = 120 MHz → prescaler 4 → 30 MHz SPI clock
 * ============================================================================ */
void MX_SPI1_Init(void)
{
    hspi1.Instance               = SPI1;
    hspi1.Init.Mode              = SPI_MODE_MASTER;
    hspi1.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;
    hspi1.Init.NSS               = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
    hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.NSSPMode          = SPI_NSS_PULSE_ENABLE;
    hspi1.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;

    if (HAL_SPI_Init(&hspi1) != HAL_OK) {
        Error_Handler();
    }
}

/* ============================================================================
 * MX_SPI2_Init  — Safety MCU (STM32G071), SPI mode 0, 5 MHz
 * APB1 = 120 MHz → prescaler 32 → 3.75 MHz SPI clock
 * ============================================================================ */
void MX_SPI2_Init(void)
{
    hspi2.Instance               = SPI2;
    hspi2.Init.Mode              = SPI_MODE_MASTER;
    hspi2.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi2.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi2.Init.CLKPolarity       = SPI_POLARITY_LOW;
    hspi2.Init.CLKPhase          = SPI_PHASE_1EDGE;
    hspi2.Init.NSS               = SPI_NSS_SOFT;
    hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
    hspi2.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi2.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi2.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    hspi2.Init.NSSPMode          = SPI_NSS_PULSE_ENABLE;
    hspi2.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;

    if (HAL_SPI_Init(&hspi2) != HAL_OK) {
        Error_Handler();
    }
}

/* ============================================================================
 * MX_UART4_Init  — GNSS receiver NMEA output, 9600 baud 8N1
 * ============================================================================ */
void MX_UART4_Init(void)
{
    huart4.Instance          = UART4;
    huart4.Init.BaudRate     = 9600;
    huart4.Init.WordLength   = UART_WORDLENGTH_8B;
    huart4.Init.StopBits     = UART_STOPBITS_1;
    huart4.Init.Parity       = UART_PARITY_NONE;
    huart4.Init.Mode         = UART_MODE_RX;       /* Rx only from GNSS module */
    huart4.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart4.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart4) != HAL_OK) {
        Error_Handler();
    }
}

/* ============================================================================
 * MX_USART3_Init  — Cellular modem AT interface, 115200 baud 8N1 with RTS/CTS
 * ============================================================================ */
void MX_USART3_Init(void)
{
    huart3.Instance          = USART3;
    huart3.Init.BaudRate     = 115200;
    huart3.Init.WordLength   = UART_WORDLENGTH_8B;
    huart3.Init.StopBits     = UART_STOPBITS_1;
    huart3.Init.Parity       = UART_PARITY_NONE;
    huart3.Init.Mode         = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl    = UART_HWCONTROL_RTS_CTS;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart3) != HAL_OK) {
        Error_Handler();
    }
}

/* ============================================================================
 * MX_I2C1_Init  — On-board sensors (temperature, battery gauge), 400 kHz FM
 * ============================================================================ */
void MX_I2C1_Init(void)
{
    hi2c1.Instance              = I2C1;
    hi2c1.Init.Timing           = 0x00C0EAFF; /* 400 kHz @ 120 MHz PCLK1 */
    hi2c1.Init.OwnAddress1      = 0;
    hi2c1.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2      = 0;
    hi2c1.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        Error_Handler();
    }
}

/* ============================================================================
 * MX_RTC_Init  — RTC in BCD format, 1 Hz calendar tick
 * ============================================================================ */
void MX_RTC_Init(void)
{
    hrtc.Instance            = RTC;
    hrtc.Init.HourFormat     = RTC_HOURFORMAT_24;
    hrtc.Init.AsynchPrediv   = 127;   /* LSE 32768 Hz / (127+1) = 256 Hz   */
    hrtc.Init.SynchPrediv    = 255;   /* 256 Hz / (255+1) = 1 Hz           */
    hrtc.Init.OutPut         = RTC_OUTPUT_DISABLE;
    hrtc.Init.OutPutRemap    = RTC_OUTPUT_REMAP_NONE;
    hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    hrtc.Init.OutPutType     = RTC_OUTPUT_TYPE_OPENDRAIN;

    if (HAL_RTC_Init(&hrtc) != HAL_OK) {
        Error_Handler();
    }
}

/* ============================================================================
 * MX_IWDG_Init  — Independent watchdog, ~1 s timeout
 * LSI ~32 kHz, prescaler 256 → 125 Hz tick → 125 counts ≈ 1 s
 * ============================================================================ */
void MX_IWDG_Init(void)
{
    hiwdg.Instance       = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
    hiwdg.Init.Reload    = IWDG_RELOAD_COUNT;
    hiwdg.Init.Window    = IWDG_WINDOW_DISABLE;

    if (HAL_IWDG_Init(&hiwdg) != HAL_OK) {
        Error_Handler();
    }
}

/* ============================================================================
 * vCANRxTask
 *
 * Monitors FDCAN Rx FIFO 0 for inbound frames from LSO modules and the TMC
 * gateway.  Translates received frames into phase commands or telemetry
 * updates and dispatches them to the appropriate queues / event bits.
 * ============================================================================ */
static void vCANRxTask(void *pvParameters)
{
    (void)pvParameters;

    FDCAN_RxHeaderTypeDef rx_header;
    uint8_t               rx_data[8];
    uint32_t              missed_ack_count = 0;
    const uint32_t        CAN_HEALTH_THRESHOLD = 3U;   /* consecutive good frames */
    uint32_t              good_frame_count = 0;

    /* Start FDCAN */
    if (HAL_FDCAN_Start(&hfdcan1) != HAL_OK) {
        fault_event_t fault = {
            .code         = FAULT_CAN_BUS_OFF,
            .severity     = FAULT_SEV_FATAL,
            .source       = FAULT_SRC_CAN_BUS,
            .timestamp_ms = HAL_GetTick(),
            .context      = {0, 0}
        };
        xQueueSend(xFaultQueue, &fault, 0);
        xEventGroupSetBits(xSystemEventGroup, SYSEVT_FAULT_ACTIVE);
        vTaskSuspend(NULL);
    }

    /* Configure acceptance filter: pass all standard frames into FIFO 0 */
    FDCAN_FilterTypeDef filter = {
        .IdType       = FDCAN_STANDARD_ID,
        .FilterIndex  = 0,
        .FilterType   = FDCAN_FILTER_RANGE,
        .FilterConfig = FDCAN_FILTER_TO_RXFIFO0,
        .FilterID1    = 0x000,
        .FilterID2    = 0x7FF
    };
    HAL_FDCAN_ConfigFilter(&hfdcan1, &filter);
    HAL_FDCAN_ConfigGlobalFilter(&hfdcan1,
                                  FDCAN_REJECT,
                                  FDCAN_REJECT,
                                  FDCAN_FILTER_REMOTE,
                                  FDCAN_FILTER_REMOTE);

    for (;;) {
        /* Poll Rx FIFO 0 */
        if (HAL_FDCAN_GetRxFifoFillLevel(&hfdcan1, FDCAN_RX_FIFO0) > 0U) {
            if (HAL_FDCAN_GetRxMessage(&hfdcan1, FDCAN_RX_FIFO0,
                                        &rx_header, rx_data) == HAL_OK) {
                good_frame_count++;
                missed_ack_count = 0;

                /* Signal CAN healthy once threshold reached */
                if (good_frame_count >= CAN_HEALTH_THRESHOLD) {
                    xEventGroupSetBits(xSystemEventGroup, SYSEVT_CAN_HEALTHY);
                }

                /* Refresh watchdog — CAN traffic proves the system is alive */
                HAL_IWDG_Refresh(&hiwdg);

                /* Dispatch by CAN ID:
                 *  0x100-0x17F : LSO status / acknowledgement frames
                 *  0x200       : TMC phase plan download trigger
                 *  0x300       : TMC manual override command           */
                if (rx_header.Identifier >= 0x100U &&
                    rx_header.Identifier <= 0x17FU) {
                    /* LSO ack frame: bytes [0]=approach_id, [1]=aspect_active */
                    telemetry_record_t tele = {
                        .timestamp_ms     = HAL_GetTick(),
                        .phase_id         = rx_data[0],
                        .state            = rx_data[1],
                        .battery_mv       = (uint16_t)((rx_data[2] << 8) | rx_data[3]),
                        .temperature_cdeg = (int16_t)((rx_data[4] << 8) | rx_data[5]),
                        .fault_flags      = ((uint32_t)rx_data[6] << 8) | rx_data[7]
                    };
                    xQueueSend(xTelemetryQueue, &tele, 0);

                } else if (rx_header.Identifier == 0x300U) {
                    /* Manual override: byte[0] = override mode, byte[1] = phase */
                    phase_cmd_msg_t cmd = {
                        .approach_id = 0xFF,   /* broadcast to all */
                        .aspect      = rx_data[1],
                        .duration_ms = 0,
                        .phase_id    = rx_data[1]
                    };
                    xQueueSend(xPhaseCommandQueue, &cmd, pdMS_TO_TICKS(5));
                }
            }
        } else {
            /* No frame — check for bus-off condition */
            FDCAN_ProtocolStatusTypeDef psr;
            HAL_FDCAN_GetProtocolStatus(&hfdcan1, &psr);
            if (psr.BusOff) {
                missed_ack_count++;
                if (missed_ack_count >= 10U) {
                    fault_event_t fault = {
                        .code         = FAULT_CAN_BUS_OFF,
                        .severity     = FAULT_SEV_FATAL,
                        .source       = FAULT_SRC_CAN_BUS,
                        .timestamp_ms = HAL_GetTick(),
                        .context      = {psr.TxErrorCnt, psr.RxErrorCnt}
                    };
                    xQueueSend(xFaultQueue, &fault, 0);
                    xEventGroupSetBits(xSystemEventGroup, SYSEVT_FAULT_ACTIVE);
                    /* Attempt recovery: init recovery sequence for bus-off */
                    HAL_FDCAN_Start(&hfdcan1);
                    missed_ack_count = 0;
                    good_frame_count = 0;
                    xEventGroupClearBits(xSystemEventGroup, SYSEVT_CAN_HEALTHY);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2));   /* 2 ms poll — ~500 frames/s headroom */
    }
}

/* ============================================================================
 * vTelemetryTask
 *
 * Drains xTelemetryQueue and forwards aggregated records to the modem over
 * UART3 as newline-delimited JSON.  On queue empty it sleeps 500 ms.
 * ============================================================================ */
static void vTelemetryTask(void *pvParameters)
{
    (void)pvParameters;

    telemetry_record_t record;
    char               json_buf[192];
    uint32_t           tx_seq = 0;

    for (;;) {
        if (xQueueReceive(xTelemetryQueue, &record, pdMS_TO_TICKS(500)) == pdTRUE) {
            int len = snprintf(json_buf, sizeof(json_buf),
                "{\"seq\":%lu,\"ts\":%lu,\"ph\":%u,"
                "\"st\":%u,\"batt\":%u,\"temp\":%d,\"fl\":%lu}\n",
                (unsigned long)tx_seq++,
                (unsigned long)record.timestamp_ms,
                record.phase_id,
                record.state,
                record.battery_mv,
                record.temperature_cdeg,
                (unsigned long)record.fault_flags);

            if (len > 0 && len < (int)sizeof(json_buf)) {
                /* Non-blocking transmit — drop if modem busy */
                HAL_UART_Transmit(&huart3, (uint8_t *)json_buf, (uint16_t)len,
                                   HAL_MAX_DELAY);
            }
        }
    }
}

/* ============================================================================
 * vFaultLogTask
 *
 * Drains xFaultQueue.  Each fault event is:
 *   1. Written to FRAM at the next available fault log slot (circular)
 *   2. Used to set/clear LED_FAULT
 *   3. Forwarded to the telemetry queue for upstream reporting
 * ============================================================================ */
static void vFaultLogTask(void *pvParameters)
{
    (void)pvParameters;

    fault_event_t  event;
    uint32_t       active_fault_count = 0;

    /* FRAM fault log base address and record size */
    static const uint32_t FRAM_FAULT_LOG_BASE  = 0x00001000UL;
    static const uint32_t FRAM_FAULT_LOG_SLOTS = 256U;
    static const uint32_t FRAM_FAULT_REC_SIZE  = sizeof(fault_event_t);
    static uint32_t       fault_log_slot       = 0;

    for (;;) {
        if (xQueueReceive(xFaultQueue, &event, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (event.code != FAULT_NONE) {
                active_fault_count++;

                /* Set fault LED */
                HAL_GPIO_WritePin(LED_FAULT_GPIO_Port, LED_FAULT_Pin, GPIO_PIN_SET);
                xEventGroupSetBits(xSystemEventGroup, SYSEVT_FAULT_ACTIVE);

                /* Write to FRAM via SPI1 (simplified: assert CS, send WRITE opcode,
                 * 3-byte address, payload, deassert CS).                           */
                uint32_t addr = FRAM_FAULT_LOG_BASE +
                                (fault_log_slot % FRAM_FAULT_LOG_SLOTS) * FRAM_FAULT_REC_SIZE;
                uint8_t fram_cmd[4];
                fram_cmd[0] = 0x02U;                        /* FRAM WRITE opcode */
                fram_cmd[1] = (uint8_t)(addr >> 16);
                fram_cmd[2] = (uint8_t)(addr >> 8);
                fram_cmd[3] = (uint8_t)(addr);

                HAL_GPIO_WritePin(FRAM_CS_GPIO_Port, FRAM_CS_Pin, GPIO_PIN_RESET);
                HAL_SPI_Transmit(&hspi1, fram_cmd, 4, HAL_MAX_DELAY);
                HAL_SPI_Transmit(&hspi1, (uint8_t *)&event, FRAM_FAULT_REC_SIZE, HAL_MAX_DELAY);
                HAL_GPIO_WritePin(FRAM_CS_GPIO_Port, FRAM_CS_Pin, GPIO_PIN_SET);

                fault_log_slot++;

                /* Forward abbreviated fault info to telemetry */
                telemetry_record_t tele = {
                    .timestamp_ms = event.timestamp_ms,
                    .phase_id     = 0,
                    .state        = (uint8_t)event.severity,
                    .battery_mv   = 0,
                    .fault_flags  = (uint32_t)event.code
                };
                xQueueSend(xTelemetryQueue, &tele, 0);

            } else {
                /* FAULT_NONE on the queue signals fault clearance */
                if (active_fault_count > 0) {
                    active_fault_count--;
                }
                if (active_fault_count == 0) {
                    HAL_GPIO_WritePin(LED_FAULT_GPIO_Port, LED_FAULT_Pin, GPIO_PIN_RESET);
                    xEventGroupClearBits(xSystemEventGroup, SYSEVT_FAULT_ACTIVE);
                }
            }
        }
    }
}

/* ============================================================================
 * FreeRTOS application hooks
 * ============================================================================ */

/**
 * @brief  Idle hook — toggles LED_STATUS at ~1 Hz as a heartbeat.
 *         Also refreshes the IWDG so a scheduler stall triggers reset.
 *         Called from the idle task; must not block.
 */
void vApplicationIdleHook(void)
{
    static uint32_t last_toggle_tick = 0;
    uint32_t now = HAL_GetTick();

    if ((now - last_toggle_tick) >= 500U) {  /* 500 ms → 1 Hz toggle */
        HAL_GPIO_TogglePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin);
        HAL_IWDG_Refresh(&hiwdg);
        last_toggle_tick = now;
    }
}

/**
 * @brief  Stack overflow hook — log fault and force watchdog reset.
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;

    /* Disable interrupts to prevent further context switches */
    taskDISABLE_INTERRUPTS();

    /* Best-effort: assert inhibit and light fault LED */
    HAL_GPIO_WritePin(INHIBIT_LINE_GPIO_Port, INHIBIT_LINE_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_FAULT_GPIO_Port,    LED_FAULT_Pin,    GPIO_PIN_SET);

    /* Let watchdog expire for a clean reset */
    for (;;) {}
}

/**
 * @brief  Malloc failed hook — called when pvPortMalloc returns NULL.
 */
void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    HAL_GPIO_WritePin(INHIBIT_LINE_GPIO_Port, INHIBIT_LINE_Pin, GPIO_PIN_SET);
    for (;;) {}
}

/**
 * @brief  HAL UART Rx complete callback — refresh IWDG on every UART packet.
 *         GNSS and modem UART activity proves the system is alive and receiving
 *         external data.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == UART4 || huart->Instance == USART3) {
        HAL_IWDG_Refresh(&hiwdg);
    }
}

/* ============================================================================
 * Error_Handler
 * ============================================================================ */
void Error_Handler(void)
{
    taskDISABLE_INTERRUPTS();

    /* Assert inhibit line — safest possible output state */
    HAL_GPIO_WritePin(INHIBIT_LINE_GPIO_Port, INHIBIT_LINE_Pin, GPIO_PIN_SET);

    /* Flash fault LED at ~10 Hz until watchdog resets the system */
    while (1) {
        HAL_GPIO_TogglePin(LED_FAULT_GPIO_Port, LED_FAULT_Pin);
        HAL_Delay(100);
    }
}

/* ============================================================================
 * assert_failed (used by HAL when USE_FULL_ASSERT is defined)
 * ============================================================================ */
#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
    Error_Handler();
}
#endif
