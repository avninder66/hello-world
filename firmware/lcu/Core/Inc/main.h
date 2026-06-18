/**
 * @file    main.h
 * @brief   LCU-100 Main Controller - STM32H743ZIT6 top-level header
 *
 * Lumina Temporary Traffic Signal Platform
 * LCU-100 Main Controller Board
 *
 * Target MCU : STM32H743ZIT6 (480 MHz Cortex-M7, 2 MB Flash, 1 MB RAM)
 * RTOS       : FreeRTOS v10.4.x (CMSIS-RTOS v2 wrapper)
 *
 * Pin assignments reflect the LCU-100 Rev C schematic.  All GPIO macros
 * follow the STM32CubeMX naming convention so generated HAL code compiles
 * without modification.
 */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Standard library includes
 * -------------------------------------------------------------------------- */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * STM32 HAL
 * -------------------------------------------------------------------------- */
#include "stm32h7xx_hal.h"

/* --------------------------------------------------------------------------
 * FreeRTOS / CMSIS-RTOS2
 * -------------------------------------------------------------------------- */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "event_groups.h"
#include "cmsis_os2.h"

/* ============================================================================
 * GPIO PIN DEFINITIONS
 *
 * Format: <signal>_Pin  / <signal>_GPIO_Port
 * All pin numbers are the STM32 HAL bit-mask form (GPIO_PIN_x).
 * ============================================================================ */

/* --- Status / Fault LEDs (GPIOB) ----------------------------------------- */
/** Green heartbeat LED, toggled by idle hook                               */
#define LED_STATUS_Pin              GPIO_PIN_0
#define LED_STATUS_GPIO_Port        GPIOB

/** Red fault indicator, set by FaultLogTask on active fault                */
#define LED_FAULT_Pin               GPIO_PIN_1
#define LED_FAULT_GPIO_Port         GPIOB

/* --- Safety MCU SPI2 chip-select and reset (GPIOC) ----------------------- */
/** Active-low SPI2 CS to STM32G071 safety supervisor                       */
#define SAFETY_MCU_CS_Pin           GPIO_PIN_6
#define SAFETY_MCU_CS_GPIO_Port     GPIOC

/** Active-low reset line to safety supervisor (open-drain, 10 kΩ pull-up) */
#define SAFETY_MCU_RESET_Pin        GPIO_PIN_7
#define SAFETY_MCU_RESET_GPIO_Port  GPIOC

/* --- CAN transceiver (GPIOA) ---------------------------------------------- */
/** Active-high shutdown to TCAN1042 transceiver (high = bus silent)        */
#define CAN_SHDN_Pin                GPIO_PIN_8
#define CAN_SHDN_GPIO_Port          GPIOA

/* --- USB Full-Speed (GPIOA) ----------------------------------------------- */
/** USB VBUS sense (input, 100 kΩ pull-down)                                */
#define USB_VBUS_Pin                GPIO_PIN_9
#define USB_VBUS_GPIO_Port          GPIOA

/** USB D- (connected to USB_OTG_FS_DM AF)                                  */
#define USB_DM_Pin                  GPIO_PIN_11
#define USB_DM_GPIO_Port            GPIOA

/** USB D+ (connected to USB_OTG_FS_DP AF)                                  */
#define USB_DP_Pin                  GPIO_PIN_12
#define USB_DP_GPIO_Port            GPIOA

/* --- FRAM SPI1 chip-select (GPIOD) --------------------------------------- */
/** Active-low SPI1 CS to MB85RS2MTPNF-G 2 Mbit FRAM                       */
#define FRAM_CS_Pin                 GPIO_PIN_14
#define FRAM_CS_GPIO_Port           GPIOD

/* --- Safety inhibit line to LSO (GPIOE) ---------------------------------- */
/**
 * Active-high inhibit output to all Lane Signal Output modules.
 * When asserted the LSO hardware overrides any phase command and drives
 * all aspects dark.  De-assert only when safety supervisor grants
 * permission.
 */
#define INHIBIT_LINE_Pin            GPIO_PIN_2
#define INHIBIT_LINE_GPIO_Port      GPIOE

/* --- Cabinet door switch (GPIOF) ----------------------------------------- */
/** Door open = logic HIGH (internal pull-down, tamper detection)           */
#define DOOR_SWITCH_Pin             GPIO_PIN_0
#define DOOR_SWITCH_GPIO_Port       GPIOF

/* ============================================================================
 * SYSTEM EVENT GROUP BIT DEFINITIONS
 *
 * xSystemEventGroup is created in main() and used across tasks to signal
 * global system state transitions.
 * ============================================================================ */
#define SYSEVT_SAFETY_READY     ( 1UL << 0 )   /**< Safety MCU self-test passed and comms healthy     */
#define SYSEVT_CAN_HEALTHY      ( 1UL << 1 )   /**< FDCAN bus open and at least one LSO acknowledged   */
#define SYSEVT_BATTERY_OK       ( 1UL << 2 )   /**< Battery voltage within operational range           */
#define SYSEVT_PHASE_ACTIVE     ( 1UL << 3 )   /**< A non-all-red phase is currently running           */
#define SYSEVT_FAULT_ACTIVE     ( 1UL << 4 )   /**< One or more active faults present in fault log     */

/* ============================================================================
 * INTER-TASK QUEUE HANDLES (defined in main.c, declared extern here)
 * ============================================================================ */

/** Phase command queue: TrafficEngine → SafetyComm / CANRx               */
extern QueueHandle_t xPhaseCommandQueue;

/** Fault event queue: any task → FaultLogTask                             */
extern QueueHandle_t xFaultQueue;

/** Telemetry data queue: TrafficEngine / CANRx → TelemetryTask            */
extern QueueHandle_t xTelemetryQueue;

/* ============================================================================
 * EVENT GROUP HANDLE
 * ============================================================================ */
extern EventGroupHandle_t xSystemEventGroup;

/* ============================================================================
 * FREERTOS TASK HANDLES (defined in main.c)
 * ============================================================================ */
extern TaskHandle_t hTrafficEngineTask;
extern TaskHandle_t hSafetyCommTask;
extern TaskHandle_t hCANRxTask;
extern TaskHandle_t hTelemetryTask;
extern TaskHandle_t hFaultLogTask;

/* ============================================================================
 * PERIPHERAL HANDLE EXTERNS (defined in peripheral init files)
 * ============================================================================ */
extern FDCAN_HandleTypeDef  hfdcan1;
extern SPI_HandleTypeDef    hspi1;      /**< FRAM SPI bus          */
extern SPI_HandleTypeDef    hspi2;      /**< Safety MCU SPI bus    */
extern UART_HandleTypeDef   huart4;     /**< GNSS NMEA UART        */
extern UART_HandleTypeDef   huart3;     /**< Modem AT UART         */
extern I2C_HandleTypeDef    hi2c1;      /**< On-board sensors      */
extern RTC_HandleTypeDef    hrtc;
extern IWDG_HandleTypeDef   hiwdg;

/* ============================================================================
 * FIRMWARE VERSION
 * ============================================================================ */
#define LCU_FW_VERSION_MAJOR    1U
#define LCU_FW_VERSION_MINOR    0U
#define LCU_FW_VERSION_PATCH    0U

/** Packed 32-bit version word: [31:16] reserved, [15:8] major, [7:4] minor, [3:0] patch */
#define LCU_FW_VERSION  ( ((uint32_t)LCU_FW_VERSION_MAJOR << 8)  | \
                          ((uint32_t)LCU_FW_VERSION_MINOR << 4)  | \
                          ((uint32_t)LCU_FW_VERSION_PATCH)       )

/* ============================================================================
 * FUNCTION PROTOTYPES
 * ============================================================================ */

/**
 * @brief  Default error handler — disables interrupts, logs context and
 *         forces a controlled system reset via the watchdog.
 *         Called by HAL_assert and peripheral init failures.
 */
void Error_Handler(void);

/* Peripheral initialisation stubs (implemented in main.c) */
void SystemClock_Config(void);
void MX_FDCAN1_Init(void);
void MX_SPI1_Init(void);
void MX_SPI2_Init(void);
void MX_UART4_Init(void);
void MX_USART3_Init(void);
void MX_I2C1_Init(void);
void MX_RTC_Init(void);
void MX_IWDG_Init(void);
void MX_GPIO_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
