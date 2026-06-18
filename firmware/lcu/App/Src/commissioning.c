/**
 * @file    commissioning.c
 * @brief   Commissioning and service UART command shell — Lumina LCU-100.
 *
 * Implements the commissioning.h public API.
 *
 * Input processing:
 *   USART3 Rx DMA fills g_rx_dma_buf in circular mode.  commissioning_task()
 *   polls the DMA NDTR register to detect new bytes and copies them into the
 *   local line buffer g_line_buf.  On CR/LF the line is null-terminated,
 *   tokenised by whitespace, and dispatched via the command table.
 *
 * Output:
 *   commissioning_send_response() formats the message as:
 *     "[LUMINA] " + message + "\r\n"
 *   and writes it via HAL_UART_Transmit() under g_usart_mutex.
 *
 * Authentication:
 *   "auth <PIN>" sets g_auth_granted_tick to HAL_GetTick().
 *   Every subsequent protected command checks that
 *     (HAL_GetTick() - g_auth_granted_tick) < AUTH_SESSION_TIMEOUT_S * 1000.
 *   AUTH_SESSION_TIMEOUT_S == 300 s by default.
 *
 * Echo:
 *   Echo is enabled by default.  "echo off" disables it; "echo on" re-enables.
 *   When echo is active each received character is immediately retransmitted
 *   over USART3 (not via the mutex-protected response path — it is fast-path
 *   single-byte HAL_UART_Transmit with a 1 ms timeout).
 *
 * Copyright (c) Lumina Traffic Systems Ltd.  All rights reserved.
 * SPDX-License-Identifier: Proprietary
 */

#include "commissioning.h"
#include "fault_log.h"
#include "fault_codes.h"
#include "power_monitor.h"
#include "safety_comm.h"
#include "traffic_engine.h"
#include "can_bus.h"
#include "main.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>

/* =========================================================================
 * External peripheral handle (defined in main.c via MX_USART3_Init)
 * ========================================================================= */
extern UART_HandleTypeDef huart3;

/* =========================================================================
 * Build metadata macros — supplied by the build system.
 * Fall back to informative defaults if not provided.
 * ========================================================================= */
#ifndef BUILD_GIT_HASH
#define BUILD_GIT_HASH  "00000000"
#endif

#ifndef BUILD_DATE
#define BUILD_DATE      __DATE__
#endif

/* =========================================================================
 * Private state
 * ========================================================================= */

/** DMA receive buffer — USART3 Rx circular DMA writes here continuously. */
static uint8_t g_rx_dma_buf[COMMISSIONING_RX_BUF_SIZE];

/**
 * Shadow read pointer into g_rx_dma_buf: last position consumed by the task.
 * Advances from 0 to COMMISSIONING_RX_BUF_SIZE - 1 and wraps.
 */
static uint16_t g_rx_shadow_pos = 0U;

/** Line assembly buffer. */
static char g_line_buf[MAX_CMD_LEN];

/** Number of bytes currently in g_line_buf. */
static uint16_t g_line_len = 0U;

/** Registered command table. */
static commissioning_command_entry_t g_cmd_table[COMMISSIONING_MAX_COMMANDS];

/** Number of commands currently registered. */
static uint8_t g_cmd_count = 0U;

/** Mutex protecting UART transmit path. */
static SemaphoreHandle_t g_usart_mutex = NULL;

/** Echo enabled flag (default true). */
static bool g_echo_enabled = true;

/**
 * HAL_GetTick() snapshot of the last successful "auth" command.
 * Zero means no session is active.
 */
static uint32_t g_auth_granted_tick = 0U;

/**
 * Configured authentication PIN (default "1234").
 * May be changed via "set auth_pin <new_pin>".
 */
static char g_auth_pin[AUTH_PIN_MAX_LEN] = AUTH_PIN_DEFAULT;

/* =========================================================================
 * Private helpers
 * ========================================================================= */

/**
 * @brief Return true if an authenticated session is currently valid.
 */
static bool prv_is_authenticated(void)
{
    if (g_auth_granted_tick == 0U)
    {
        return false;
    }
    uint32_t elapsed_ms = HAL_GetTick() - g_auth_granted_tick;
    return (elapsed_ms < ((uint32_t)AUTH_SESSION_TIMEOUT_S * 1000U));
}

/**
 * @brief Send a printf-style formatted response.
 *
 * Formats into a stack buffer then calls commissioning_send_response().
 * Maximum formatted length is MAX_CMD_LEN - 1 characters.
 */
static void prv_sendf(const char *fmt, ...)
{
    char buf[MAX_CMD_LEN];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    commissioning_send_response(buf);
}

/**
 * @brief Tokenise the null-terminated line in place into argv[].
 *
 * Tokens are separated by space or tab.  Modifies @p line by inserting NUL
 * bytes after each token.  Returns the token count (argc).
 *
 * @param line  Mutable null-terminated input string.
 * @param argv  Caller-supplied pointer array of at least MAX_ARGS entries.
 * @return      Number of tokens found (0 if the line is empty).
 */
static int prv_tokenise(char *line, char *argv[MAX_ARGS])
{
    int argc = 0;
    char *p  = line;

    while (*p != '\0' && argc < MAX_ARGS)
    {
        /* Skip leading whitespace. */
        while (*p == ' ' || *p == '\t') { p++; }
        if (*p == '\0') { break; }

        argv[argc++] = p;

        /* Advance to end of token. */
        while (*p != ' ' && *p != '\t' && *p != '\0') { p++; }
        if (*p != '\0')
        {
            *p = '\0';
            p++;
        }
    }
    return argc;
}

/**
 * @brief Echo a single character back to USART3 (fast-path, no mutex).
 */
static void prv_echo_char(uint8_t c)
{
    /* 1 ms timeout — character echo must not stall the task. */
    (void)HAL_UART_Transmit(&huart3, &c, 1U, 1U);
}

/**
 * @brief Return the current DMA read position (bytes consumed by hardware).
 *
 * STM32H7 USART3 DMA: we started with NDTR == COMMISSIONING_RX_BUF_SIZE.
 * Each byte received decrements NDTR by one.  Write position in the buffer =
 * COMMISSIONING_RX_BUF_SIZE - NDTR.
 */
static uint16_t prv_dma_write_pos(void)
{
    /* hdma_usart3_rx is the DMA handle for USART3 Rx — declared as extern in
     * the STM32CubeMX-generated dma.c / main.c. */
    extern DMA_HandleTypeDef hdma_usart3_rx;
    uint16_t ndtr = (uint16_t)(__HAL_DMA_GET_COUNTER(&hdma_usart3_rx));
    return (uint16_t)(COMMISSIONING_RX_BUF_SIZE - ndtr);
}

/* =========================================================================
 * Built-in command handlers
 * ========================================================================= */

/* ---- "help" -------------------------------------------------------------- */
static int prv_cmd_help(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    commissioning_send_response("Registered commands:");
    for (uint8_t i = 0U; i < g_cmd_count; i++)
    {
        char line[MAX_CMD_LEN];
        (void)snprintf(line, sizeof(line), "  %-14s  %s%s",
                       g_cmd_table[i].name,
                       g_cmd_table[i].help_string,
                       g_cmd_table[i].requires_auth ? "  [AUTH]" : "");
        commissioning_send_response(line);
    }
    return 0;
}

/* ---- "status" ------------------------------------------------------------ */
static int prv_cmd_status(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    /* Firmware version. */
    prv_sendf("Firmware  : v%u.%u.%u",
              (unsigned)LCU_FW_VERSION_MAJOR,
              (unsigned)LCU_FW_VERSION_MINOR,
              (unsigned)LCU_FW_VERSION_PATCH);

    /* Uptime. */
    uint32_t uptime_s = HAL_GetTick() / 1000U;
    prv_sendf("Uptime    : %lu s (%lu h %lu m %lu s)",
              (unsigned long)uptime_s,
              (unsigned long)(uptime_s / 3600U),
              (unsigned long)((uptime_s % 3600U) / 60U),
              (unsigned long)(uptime_s % 60U));

    /* Traffic engine state. */
    traffic_state_t te_state = traffic_engine_get_state();
    prv_sendf("TE State  : %d", (int)te_state);

    /* Battery. */
    battery_status_t batt;
    power_monitor_get_status(&batt);
    prv_sendf("Battery   : %u mV  SoC %u%%  %s",
              (unsigned)batt.voltage_mv,
              (unsigned)batt.soc_pct,
              batt.charger_active ? "CHARGING" : "DISCHARGING");

    /* Fault count. */
    prv_sendf("Faults    : %lu logged", (unsigned long)fault_log_get_count());

    /* CAN bus. */
    can_error_state_t can_err = can_bus_get_error_state();
    prv_sendf("CAN bus   : %s",
              (can_err == CAN_ERROR_NONE)    ? "OK" :
              (can_err == CAN_ERROR_WARNING)  ? "WARNING" :
              (can_err == CAN_ERROR_PASSIVE)  ? "PASSIVE" :
                                                "BUS-OFF");
    return 0;
}

/* ---- "phase N" ----------------------------------------------------------- */
static int prv_cmd_phase(int argc, char *argv[])
{
    if (argc < 2)
    {
        commissioning_send_response("Usage: phase <N>");
        return -1;
    }

    long phase_id = strtol(argv[1], NULL, 10);
    if (phase_id < 0 || phase_id > 255)
    {
        commissioning_send_response("ERROR: phase ID out of range (0-255)");
        return -1;
    }

    phase_request_t req;
    req.requested_phase_id = (uint8_t)phase_id;
    req.reason             = REASON_MANUAL;

    if (xQueueSend(xPhaseCommandQueue, &req, pdMS_TO_TICKS(100U)) != pdTRUE)
    {
        commissioning_send_response("ERROR: phase queue full");
        return -1;
    }

    prv_sendf("Phase %ld requested", phase_id);
    return 0;
}

/* ---- "mode all_red|fixed|demand|blackout" -------------------------------- */
static int prv_cmd_mode(int argc, char *argv[])
{
    if (argc < 2)
    {
        commissioning_send_response("Usage: mode all_red|fixed|demand|blackout");
        return -1;
    }

    traffic_engine_mode_t mode;
    if (strcmp(argv[1], "all_red") == 0)
    {
        mode = TE_MODE_NORMAL;  /* all-red is the normal idle state */
        /* Post an explicit all-red phase request. */
        phase_request_t req;
        req.requested_phase_id = 0U;
        req.reason             = REASON_MANUAL;
        (void)xQueueSend(xPhaseCommandQueue, &req, pdMS_TO_TICKS(100U));
    }
    else if (strcmp(argv[1], "fixed") == 0)
    {
        mode = TE_MODE_NORMAL;
    }
    else if (strcmp(argv[1], "demand") == 0)
    {
        mode = TE_MODE_NORMAL;
    }
    else if (strcmp(argv[1], "blackout") == 0)
    {
        mode = TE_MODE_BLACKOUT;
    }
    else
    {
        commissioning_send_response("ERROR: unknown mode");
        return -1;
    }

    traffic_engine_set_mode(mode);
    prv_sendf("Mode set: %s", argv[1]);
    return 0;
}

/* ---- "faults" ------------------------------------------------------------ */
static int prv_cmd_faults(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    commissioning_send_response("--- Fault log dump ---");
    fault_log_dump_uart(0U);
    commissioning_send_response("--- End of fault log ---");
    return 0;
}

/* ---- "info" -------------------------------------------------------------- */
static int prv_cmd_info(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    prv_sendf("Git hash  : %s", BUILD_GIT_HASH);
    prv_sendf("Build date: %s", BUILD_DATE);
    prv_sendf("FW version: v%u.%u.%u",
              (unsigned)LCU_FW_VERSION_MAJOR,
              (unsigned)LCU_FW_VERSION_MINOR,
              (unsigned)LCU_FW_VERSION_PATCH);
    /* Board serial from FRAM would require a FRAM read here; placeholder: */
    commissioning_send_response("Board SN  : (read from FRAM)");
    return 0;
}

/* ---- "reboot" ------------------------------------------------------------ */
static int prv_cmd_reboot(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    commissioning_send_response("Rebooting in 1 s...");
    vTaskDelay(pdMS_TO_TICKS(1000U));
    NVIC_SystemReset();

    /* Unreachable — suppress compiler warning. */
    return 0;
}

/* ---- "selftest" ---------------------------------------------------------- */
static int prv_cmd_selftest(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    commissioning_send_response("Triggering safety supervisor self-test...");

    bool passed = safety_comm_trigger_self_test();

    if (passed)
    {
        commissioning_send_response("Self-test PASSED");
    }
    else
    {
        commissioning_send_response("Self-test FAILED");
    }
    return passed ? 0 : -1;
}

/* ---- "auth <PIN>" -------------------------------------------------------- */
static int prv_cmd_auth(int argc, char *argv[])
{
    if (argc < 2)
    {
        commissioning_send_response("Usage: auth <PIN>");
        return -1;
    }

    if (strcmp(argv[1], g_auth_pin) == 0)
    {
        g_auth_granted_tick = HAL_GetTick();
        prv_sendf("Authenticated. Session valid for %u s.",
                  (unsigned)AUTH_SESSION_TIMEOUT_S);
        return 0;
    }
    else
    {
        g_auth_granted_tick = 0U;
        commissioning_send_response("ERROR: incorrect PIN");
        return -1;
    }
}

/* ---- "echo on|off" ------------------------------------------------------- */
static int prv_cmd_echo(int argc, char *argv[])
{
    if (argc < 2)
    {
        prv_sendf("Echo: %s", g_echo_enabled ? "on" : "off");
        return 0;
    }

    if (strcmp(argv[1], "on") == 0)
    {
        g_echo_enabled = true;
        commissioning_send_response("Echo enabled");
    }
    else if (strcmp(argv[1], "off") == 0)
    {
        g_echo_enabled = false;
        commissioning_send_response("Echo disabled");
    }
    else
    {
        commissioning_send_response("Usage: echo on|off");
        return -1;
    }
    return 0;
}

/* ---- "dfu" --------------------------------------------------------------- */
static int prv_cmd_dfu(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    commissioning_send_response("Entering DFU bootloader in 1 s...");
    vTaskDelay(pdMS_TO_TICKS(1000U));

    /* STM32H743 system bootloader lives at 0x1FF09800.
     * Disable interrupts, remap to system memory, then branch. */
    __disable_irq();
    HAL_RCC_DeInit();
    HAL_DeInit();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL  = 0U;

    /* Set MSP and jump to bootloader reset handler. */
    typedef void (*pFunction)(void);
    const uint32_t BOOTLOADER_ADDR = 0x1FF09800UL;
    uint32_t       JumpAddress     = *((volatile uint32_t *)(BOOTLOADER_ADDR + 4U));
    pFunction      JumpToBootloader = (pFunction)JumpAddress;
    __set_MSP(*((volatile uint32_t *)BOOTLOADER_ADDR));
    JumpToBootloader();

    /* Unreachable. */
    return 0;
}

/* =========================================================================
 * Dispatch helper
 * ========================================================================= */

/**
 * @brief Find and call the handler matching argv[0].
 *
 * Checks authentication for protected commands before dispatching.
 */
static void prv_dispatch(int argc, char *argv[])
{
    if (argc == 0 || argv[0] == NULL || argv[0][0] == '\0')
    {
        return;
    }

    for (uint8_t i = 0U; i < g_cmd_count; i++)
    {
        if (strcmp(g_cmd_table[i].name, argv[0]) == 0)
        {
            if (g_cmd_table[i].requires_auth && !prv_is_authenticated())
            {
                commissioning_send_response(
                    "ERROR: authentication required — use: auth <PIN>");
                return;
            }
            (void)g_cmd_table[i].handler(argc, argv);
            return;
        }
    }

    prv_sendf("ERROR: unknown command '%s' — type 'help'", argv[0]);
}

/* =========================================================================
 * Public API
 * ========================================================================= */

bool commissioning_init(void)
{
    g_cmd_count        = 0U;
    g_echo_enabled     = true;
    g_auth_granted_tick = 0U;
    g_rx_shadow_pos    = 0U;
    g_line_len         = 0U;
    memset(g_line_buf, 0, sizeof(g_line_buf));
    memset(g_cmd_table, 0, sizeof(g_cmd_table));
    (void)strncpy(g_auth_pin, AUTH_PIN_DEFAULT, sizeof(g_auth_pin) - 1U);

    g_usart_mutex = xSemaphoreCreateMutex();
    if (g_usart_mutex == NULL)
    {
        return false;
    }

    /* Start USART3 Rx in DMA circular mode so bytes are captured without
     * polling.  The DMA handle and peripheral must be initialised by
     * MX_USART3_Init() / MX_DMA_Init() before calling this function. */
    if (HAL_UART_Receive_DMA(&huart3,
                             g_rx_dma_buf,
                             COMMISSIONING_RX_BUF_SIZE) != HAL_OK)
    {
        return false;
    }

    /* Register built-in handlers. */
    (void)commissioning_register_handler("help",     prv_cmd_help,     "List commands",                         false);
    (void)commissioning_register_handler("status",   prv_cmd_status,   "System status summary",                 false);
    (void)commissioning_register_handler("phase",    prv_cmd_phase,    "Request phase N",                       true);
    (void)commissioning_register_handler("mode",     prv_cmd_mode,     "Set mode: all_red|fixed|demand|blackout", true);
    (void)commissioning_register_handler("faults",   prv_cmd_faults,   "Dump FRAM fault log",                   false);
    (void)commissioning_register_handler("info",     prv_cmd_info,     "Firmware git hash, build date, board SN", false);
    (void)commissioning_register_handler("reboot",   prv_cmd_reboot,   "Reboot controller (1 s delay)",         true);
    (void)commissioning_register_handler("selftest", prv_cmd_selftest, "Trigger safety supervisor self-test",   false);
    (void)commissioning_register_handler("auth",     prv_cmd_auth,     "Authenticate: auth <PIN>",              false);
    (void)commissioning_register_handler("echo",     prv_cmd_echo,     "Toggle echo: echo on|off",              false);
    (void)commissioning_register_handler("dfu",      prv_cmd_dfu,      "Enter DFU bootloader",                  true);

    return true;
}

/* -------------------------------------------------------------------------
 * commissioning_task
 *
 * Reads bytes from the DMA ring buffer, echoes them if enabled, assembles
 * lines terminated by CR or LF, and dispatches complete lines.
 * ------------------------------------------------------------------------- */
void commissioning_task(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        /* Poll the DMA write position at 5 ms intervals. */
        vTaskDelay(pdMS_TO_TICKS(5U));

        uint16_t write_pos = prv_dma_write_pos();

        /* Process all bytes that arrived since our last read. */
        while (g_rx_shadow_pos != write_pos)
        {
            uint8_t c = g_rx_dma_buf[g_rx_shadow_pos];

            /* Advance shadow pointer with wrap. */
            g_rx_shadow_pos =
                (uint16_t)((g_rx_shadow_pos + 1U) % COMMISSIONING_RX_BUF_SIZE);

            /* Echo the character immediately if enabled. */
            if (g_echo_enabled)
            {
                prv_echo_char(c);
                /* Send CR+LF on Enter to move cursor to next line. */
                if (c == '\r')
                {
                    prv_echo_char('\n');
                }
            }

            /* Ignore bare LF when we already dispatched on the preceding CR. */
            if (c == '\n')
            {
                continue;
            }

            /* Backspace / DEL — remove last character from buffer. */
            if (c == '\b' || c == 0x7FU)
            {
                if (g_line_len > 0U)
                {
                    g_line_len--;
                }
                continue;
            }

            /* Line terminator. */
            if (c == '\r')
            {
                /* Null-terminate and dispatch. */
                g_line_buf[g_line_len] = '\0';

                if (g_line_len > 0U)
                {
                    char *argv[MAX_ARGS];
                    int   argc = prv_tokenise(g_line_buf, argv);
                    prv_dispatch(argc, argv);
                }

                g_line_len = 0U;
                continue;
            }

            /* Printable character — append to line buffer if room. */
            if (c >= 0x20U && c < 0x7FU)
            {
                if (g_line_len < (MAX_CMD_LEN - 1U))
                {
                    g_line_buf[g_line_len++] = (char)c;
                }
                /* Silently drop characters beyond buffer limit. */
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * commissioning_register_handler
 * ------------------------------------------------------------------------- */
bool commissioning_register_handler(const char    *name,
                                    cmd_handler_t  handler,
                                    const char    *help_string,
                                    bool           requires_auth)
{
    if (name == NULL || handler == NULL)
    {
        return false;
    }

    /* Check for existing entry with the same name and replace it. */
    for (uint8_t i = 0U; i < g_cmd_count; i++)
    {
        if (strcmp(g_cmd_table[i].name, name) == 0)
        {
            g_cmd_table[i].handler      = handler;
            g_cmd_table[i].requires_auth = requires_auth;
            if (help_string != NULL)
            {
                (void)strncpy(g_cmd_table[i].help_string,
                              help_string,
                              COMMISSIONING_HELP_STR_LEN - 1U);
            }
            return true;
        }
    }

    if (g_cmd_count >= COMMISSIONING_MAX_COMMANDS)
    {
        return false;
    }

    commissioning_command_entry_t *entry = &g_cmd_table[g_cmd_count];
    (void)strncpy(entry->name, name, COMMISSIONING_CMD_NAME_LEN - 1U);
    entry->name[COMMISSIONING_CMD_NAME_LEN - 1U] = '\0';

    entry->handler       = handler;
    entry->requires_auth = requires_auth;

    if (help_string != NULL)
    {
        (void)strncpy(entry->help_string,
                      help_string,
                      COMMISSIONING_HELP_STR_LEN - 1U);
        entry->help_string[COMMISSIONING_HELP_STR_LEN - 1U] = '\0';
    }
    else
    {
        entry->help_string[0] = '\0';
    }

    g_cmd_count++;
    return true;
}

/* -------------------------------------------------------------------------
 * commissioning_send_response
 * ------------------------------------------------------------------------- */
void commissioning_send_response(const char *message)
{
    if (message == NULL)
    {
        return;
    }

    static const char prefix[] = "[LUMINA] ";
    static const char suffix[] = "\r\n";

    if (xSemaphoreTake(g_usart_mutex, pdMS_TO_TICKS(50U)) == pdTRUE)
    {
        /* Transmit prefix. */
        (void)HAL_UART_Transmit(&huart3,
                                (const uint8_t *)prefix,
                                (uint16_t)(sizeof(prefix) - 1U),
                                100U);

        /* Transmit message body. */
        uint16_t len = (uint16_t)strnlen(message, MAX_CMD_LEN);
        (void)HAL_UART_Transmit(&huart3,
                                (const uint8_t *)message,
                                len,
                                500U);

        /* Transmit CRLF suffix. */
        (void)HAL_UART_Transmit(&huart3,
                                (const uint8_t *)suffix,
                                (uint16_t)(sizeof(suffix) - 1U),
                                100U);

        xSemaphoreGive(g_usart_mutex);
    }
}
